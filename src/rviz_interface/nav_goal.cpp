/* includes //{ */

#include <ros/ros.h>
#include <nodelet/nodelet.h>

#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2/LinearMath/Quaternion.h>

#include <geometry_msgs/PoseStamped.h>

#include <nav_msgs/Odometry.h>

#include <mrs_msgs/MpcTrackerDiagnostics.h>
#include <mrs_msgs/ReferenceStampedSrv.h>

#include <mrs_lib/param_loader.h>
#include <mrs_lib/profiler.h>
#include <mrs_lib/mutex.h>
#include <mrs_lib/attitude_converter.h>

#include <mrs_lib/subscribe_handler.h>
#include <mrs_lib/transformer.h>

//}

namespace mrs_rviz_plugins
{

namespace rviz_interface
{

/* class NavGoal //{ */

class NavGoal : public nodelet::Nodelet {

public:
  virtual void onInit();

private:
  ros::NodeHandle nh_;
  bool            is_initialized_ = false;

  // | ---------------------- msg callbacks --------------------- |

private:
  mrs_lib::SubscribeHandler<geometry_msgs::PoseStamped> sh_rviz_goal_;
  void                                                  callbackRvizNavGoal(mrs_lib::SubscribeHandler<geometry_msgs::PoseStamped>& wrp);

  mrs_lib::SubscribeHandler<nav_msgs::Odometry> sh_cmd_odom_;
  void                                          callbackCmdOdomUav(mrs_lib::SubscribeHandler<nav_msgs::Odometry>& wrp);

  void timeoutGeneric(const std::string& topic, const ros::Time& last_msg, [[maybe_unused]] const int n_pubs);

  void               callbackOdomUav(const nav_msgs::OdometryConstPtr& msg);
  nav_msgs::Odometry odom_uav_;
  bool               got_odom_uav_ = false;
  std::mutex         mutex_odom_uav_;

  mrs_lib::Transformer transformer_;

private:
  mrs_lib::Profiler* profiler_;
  bool               _profiler_enabled_ = false;

  ros::ServiceClient srv_client_reference_;
};

//}

/* onInit() //{ */

void NavGoal::onInit() {

  /* obtain node handle */
  nh_ = nodelet::Nodelet::getMTPrivateNodeHandle();

  /* waits for the ROS to publish clock */
  ros::Time::waitForValid();

  // | ------------------- load ros parameters ------------------ |
  mrs_lib::ParamLoader param_loader(nh_, "NavGoal");

  param_loader.loadParam("enable_profiler", _profiler_enabled_);

  transformer_ = mrs_lib::Transformer("NavGoal");
  transformer_.setLookupTimeout(ros::Duration(1.0));

  mrs_lib::SubscribeHandlerOptions shopts;
  shopts.nh                 = nh_;
  shopts.node_name          = "NavGoal";
  shopts.no_message_timeout = mrs_lib::no_timeout;
  shopts.threadsafe         = true;
  shopts.autostart          = true;
  shopts.queue_size         = 5;
  shopts.transport_hints    = ros::TransportHints().tcpNoDelay();


  // | ----------------------- subscribers ---------------------- |
  sh_rviz_goal_ = mrs_lib::SubscribeHandler<geometry_msgs::PoseStamped>(shopts, "rviz_nav_goal_in", &NavGoal::callbackRvizNavGoal, this);
  sh_cmd_odom_  = mrs_lib::SubscribeHandler<nav_msgs::Odometry>(shopts, "cmd_odom_in", ros::Duration(1.0), &NavGoal::timeoutGeneric, this,
                                                               &NavGoal::callbackCmdOdomUav, this);

  // | --------------- initialize service clients --------------- |
  srv_client_reference_ = nh_.serviceClient<mrs_msgs::ReferenceStampedSrv>("reference_service_out");

  // | ----------------------- finish init ---------------------- |
  is_initialized_ = true;

  ROS_INFO_ONCE("[NavGoal]: initialized");

  ROS_INFO("[RvizPoseEstimate]: Waiting for user input in rviz...");
}
//}

// | ---------------------- msg callbacks --------------------- |

/* callbackRvizNavGoal() //{ */

void NavGoal::callbackRvizNavGoal(mrs_lib::SubscribeHandler<geometry_msgs::PoseStamped>& wrp) {

  /* do not continue if the nodelet is not initialized */
  if (!is_initialized_) {
    return;
  }

  if (!got_odom_uav_) {
    ROS_WARN("[NavGoal]: Haven't received UAV odometry yet, skipping goal.");
    return;
  }

  geometry_msgs::PoseStampedConstPtr goal_ptr = wrp.getMsg();
  geometry_msgs::PoseStamped         goal     = *goal_ptr;

  {
    std::scoped_lock lock(mutex_odom_uav_);

    // transform UAV odometry to reference frame of the goal
    auto tf = transformer_.getTransform(odom_uav_.header.frame_id, goal.header.frame_id, odom_uav_.header.stamp);
    if (tf) {
      geometry_msgs::PoseStamped pose_uav;
      pose_uav.header = odom_uav_.header;
      pose_uav.pose   = odom_uav_.pose.pose;
      auto res        = transformer_.transform(pose_uav, tf.value());
      if (res) {
        // set z-coordinate of the goal to be the same as in cmd odom
        goal.pose.position.z = res.value().pose.position.z;
        ROS_INFO("[NavGoal]: Setting z = %.3f m to the goal, frame_id of goal: %s", goal.pose.position.z, goal.header.frame_id.c_str());
      } else {
        ROS_WARN("[NavGoal]: Unable to transform cmd odom from %s to %s at time %.6f.", odom_uav_.header.frame_id.c_str(), goal.header.frame_id.c_str(),
                 odom_uav_.header.stamp.toSec());
        return;
      }
    } else {
      ROS_WARN("[NavGoal]: Unable to find transform from %s to %s at time %.6f.", odom_uav_.header.frame_id.c_str(), goal.header.frame_id.c_str(),
               odom_uav_.header.stamp.toSec());
      return;
    }
  }

  // publish the goal to the service
  // Create new waypoint msg
  mrs_msgs::ReferenceStampedSrv new_waypoint;
  new_waypoint.request.header.frame_id    = goal.header.frame_id;
  new_waypoint.request.header.stamp       = ros::Time::now();
  new_waypoint.request.reference.position = goal.pose.position;

  try {
    new_waypoint.request.reference.heading = mrs_lib::AttitudeConverter(goal.pose.orientation).getHeading();
  }
  catch (mrs_lib::AttitudeConverter::GetHeadingException& e) {
    /* new_waypoint.request.reference.heading = 0; */
    ROS_ERROR("[NavGoal]: Unable to calculate heading from quaternion: [%.3f %.3f %.3f %.3f]", goal.pose.orientation.x, goal.pose.orientation.y,
              goal.pose.orientation.z, goal.pose.orientation.w);
    return;
  }

  ROS_INFO("[NavGoal]: Calling reference service with point [%.3f %.3f %.3f], heading: %.3f", new_waypoint.request.reference.position.x,
           new_waypoint.request.reference.position.y, new_waypoint.request.reference.position.z, new_waypoint.request.reference.heading);
  bool success = srv_client_reference_.call(new_waypoint);
  ROS_INFO("[NavGoal]: Reference service response: %s", new_waypoint.response.message.c_str());

  if (!success || !new_waypoint.response.success) {
    ROS_ERROR("[NavGoal]: Could not set reference.");
    return;
  }
}

//}

/* callbackOdomUav() //{ */

void NavGoal::callbackCmdOdomUav(mrs_lib::SubscribeHandler<nav_msgs::Odometry>& wrp) {

  /* do not continue if the nodelet is not initialized */
  if (!is_initialized_)
    return;

  {
    std::scoped_lock lock(mutex_odom_uav_);

    nav_msgs::OdometryConstPtr odom_ptr = wrp.getMsg();
    odom_uav_                           = *odom_ptr;
  }

  if (!got_odom_uav_) {
    got_odom_uav_ = true;
  }
}

//}

/* timeoutGeneric() */ /*//{*/
void NavGoal::timeoutGeneric(const std::string& topic, const ros::Time& last_msg, [[maybe_unused]] const int n_pubs) {

  ROS_WARN_THROTTLE(1.0, "[NavGoal]: not receiving '%s' for %.3f s", topic.c_str(), (ros::Time::now() - last_msg).toSec());
}
/*//}*/

}  // namespace rviz_interface

}  // namespace mrs_rviz_plugins

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(mrs_rviz_plugins::rviz_interface::NavGoal, nodelet::Nodelet);
