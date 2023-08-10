#include "control/drone_entity.h"

using namespace visualization_msgs;

namespace mrs_rviz_plugins{

DroneEntity::DroneEntity(const std::string name_){
  name = name_;
  nh = ros::NodeHandle(name + "/mrs_uav_status/");
  status_subscriber = nh.subscribe("uav_status", 1, &DroneEntity::statusCallback, this, ros::TransportHints().tcpNoDelay());
  server = new interactive_markers::InteractiveMarkerServer("control", name.c_str(), true);
  
  // Regular marker
  Marker marker_msg;
  marker_msg.id              = 1;
  marker_msg.type            = Marker::CUBE;
  marker_msg.scale.x         = 0.45;
  marker_msg.scale.y         = 0.45;
  marker_msg.scale.z         = 0.45;
  marker_msg.color.r         = 0.5;
  marker_msg.color.g         = 0.5;
  marker_msg.color.b         = 0.5;
  marker_msg.color.a         = 0.0;        // Absolutely transparent
  marker_msg.pose.position.x = 0;
  marker_msg.pose.position.y = 0;
  marker_msg.pose.position.z = 0;

  // Control
  InteractiveMarkerControl control_msg;
  control_msg.name                           = "control1";
  control_msg.always_visible                 = true;
  control_msg.always_visible                 = true;
  control_msg.interaction_mode               = InteractiveMarkerControl::BUTTON;
  control_msg.orientation_mode               = InteractiveMarkerControl::INHERIT;
  control_msg.independent_marker_orientation = true;
  control_msg.markers.push_back(marker_msg);

  // Interactive marker
  InteractiveMarker int_marker_msg;
  int_marker_msg.name            = name + " marker";
  int_marker_msg.scale           = 1;
  int_marker_msg.header.frame_id = name + "/fcu";
  int_marker_msg.pose.position.y = 0;
  int_marker_msg.controls.push_back(control_msg);
  server->insert(int_marker_msg);
  
  updateMenu();
  
  
}

bool DroneEntity::compareAndUpdate(std::vector<std::string>& current, const std::vector<std::string>& actual) {
  if(current.size() != actual.size()){
    current = actual;
    return true;
  }

  for(int i=0; i<current.size(); i++){
    if(current[i] != actual[i]){
      current = actual;
      return true;
    }
  }

  return false;
}

void DroneEntity::updateMenu(){
  // TODO: make some of first 3 etries invisible according to their current state
  
  // This seems to be inefficient, but must work. And since no 
  // clearEntry() method is present, it's the only option
  if(menu_handler != nullptr){
    delete menu_handler;
  }
  menu_handler = new interactive_markers::MenuHandler();

  entries.resize(EntryIndex::SIZE);
  entries[LAND             ] = menu_handler->insert("Land", [this](const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){land(feedback);});
  entries[LAND_HOME        ] = menu_handler->insert("Land Home", [this](const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){landHome(feedback);});
  entries[TAKEOFF          ] = menu_handler->insert("Takeoff", [this](const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){takeoff(feedback);});
  entries[SET_CONSTRAINT   ] = menu_handler->insert("Set Constraint");
  entries[SET_GAIN         ] = menu_handler->insert("Set Gains");
  entries[SET_CONTROLLER   ] = menu_handler->insert("Set Controller");
  entries[SET_TRACKER      ] = menu_handler->insert("Set Tracker");
  entries[SET_ODOM_SOURCE  ] = menu_handler->insert("Set Odom Source");
  entries[SET_LAT_ESTIMATOR] = menu_handler->insert("Set Lat Estimator");
  entries[SET_ALT_ESTIMATOR] = menu_handler->insert("Set Alt Estimator");
  entries[SET_HDG_ESTIMATOR] = menu_handler->insert("Set Hdg Estimator");

  for(const auto constraint : constraints){
    menu_handler->insert(entries[SET_CONSTRAINT], constraint, 
      [this, constraint]
      (const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){
        setConstraint(constraint, feedback);
      });
  }

  for(const auto gain : gains){
    menu_handler->insert(entries[SET_GAIN], gain, 
      [this, gain]
      (const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){
        setGain(gain, feedback);
      });
  }

  for(const auto controller : controllers){
    menu_handler->insert(entries[SET_CONTROLLER], controller, 
      [this, controller]
      (const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){
        setController(controller, feedback);
      });
  }

  for(const auto tracker : trackers){
    menu_handler->insert(entries[SET_TRACKER], tracker, 
      [this, tracker]
      (const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){
        setTracker(tracker, feedback);
      });
  }

  for(const auto odom_source : odom_lat_sources){
    menu_handler->insert(entries[SET_ODOM_SOURCE], odom_source, 
      [this, odom_source]
      (const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){
        setOdomSource(odom_source, feedback);
      });
  }

  for(const auto lat_estimator : odom_lat_sources){
    menu_handler->insert(entries[SET_LAT_ESTIMATOR], lat_estimator, 
      [this, lat_estimator]
      (const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){
        setLatEstimator(lat_estimator, feedback);
      });
  }

  for(const auto alt_estimator : odom_alt_sources){
    menu_handler->insert(entries[SET_ALT_ESTIMATOR], alt_estimator, 
      [this, alt_estimator]
      (const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){
        setAltEstimator(alt_estimator, feedback);
      });
  }

  for(const auto hdg_estimator : odom_hdg_sources){
    menu_handler->insert(entries[SET_HDG_ESTIMATOR], hdg_estimator, 
      [this, hdg_estimator]
      (const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback){
        setHdgEstimator(hdg_estimator, feedback);
      });
  }

  menu_handler->apply(*server, name + " marker");
  server->applyChanges();
  ROS_INFO("Menu options updated");
}

void DroneEntity::statusCallback(const mrs_msgs::UavStatusConstPtr& msg) {
  // Note: This might run simultaniously with other method reading from these vectors
  bool updated = false;

  updated |= compareAndUpdate(constraints, msg->constraints);
  updated |= compareAndUpdate(gains, msg->gains);
  updated |= compareAndUpdate(controllers, msg->controllers);
  updated |= compareAndUpdate(trackers, msg->trackers);
  updated |= compareAndUpdate(odom_lat_sources, msg->odom_estimators_hori);
  updated |= compareAndUpdate(odom_alt_sources, msg->odom_estimators_vert);
  updated |= compareAndUpdate(odom_hdg_sources, msg->odom_estimators_hdg);

  if(updated){
    updateMenu();
  }
}

// Menu callbacks
// static
 void DroneEntity::land(const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called land");
}
// static
 void DroneEntity::landHome(const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called landhome");
}
// static
 void DroneEntity::takeoff(const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called takeof");
}
// static
 void DroneEntity::setConstraint(std::string value, const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called constraint %s", value.c_str());
}
// static
 void DroneEntity::setGain(std::string value, const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called gains %s", value.c_str());
}
// static
 void DroneEntity::setController(std::string value, const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called controller %s", value.c_str());
}
// static
 void DroneEntity::setTracker(std::string value, const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called tracker %s", value.c_str());
}
// static
 void DroneEntity::setOdomSource(std::string value, const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called odom source %s", value.c_str());
}
// static
 void DroneEntity::setLatEstimator(std::string value, const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called lat estim %s", value.c_str());
}
// static
 void DroneEntity::setAltEstimator(std::string value, const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called alt estim %s", value.c_str());
}
// static
 void DroneEntity::setHdgEstimator(std::string value, const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback) {
  ROS_INFO("Menu callback called hdg estim %s", value.c_str());
}
 
std::vector<std::string> DroneEntity::getConstraints() {
  return constraints;
}
std::vector<std::string> DroneEntity::getGains() {
  return gains;
}
std::vector<std::string> DroneEntity::getControllers() {
  return controllers;
}
std::vector<std::string> DroneEntity::getTrackers() {
  return trackers;
}
std::vector<std::string> DroneEntity::getOdomSources() {
  return odom_lat_sources;
}
std::vector<std::string> DroneEntity::getLatEstimators() {
  return odom_lat_sources;
}
std::vector<std::string> DroneEntity::getAltEstimators() {
  return odom_alt_sources;
}
std::vector<std::string> DroneEntity::getHdgEstimators() {
  return odom_hdg_sources;
}

}// namespace mrs_rviz_plugins