#include <coverage_path_planning/exact_decomposition.h>

// TODO: include polygon only
// #include <mrs_lib/safety_zone/polygon.h>
#include <mrs_lib/safety_zone/prism.h>

#include <rviz/ogre_helpers/line.h>

namespace bg = boost::geometry;

namespace mrs_rviz_plugins{

void ExactDecomposition::drawCurrentPolygon(){
  if(boundaries_node_){
    scene_manager_->destroySceneNode(boundaries_node_);
  }
  boundaries_node_ = root_node_->createChildSceneNode();

  const auto& tf = transformer_.getTransform(polygon_frame_, current_frame_);
  if (!tf) {
    ROS_INFO("[ExactDecomposition]: Transformation is not found. boundaries will not be displayed");
    return;
  }
  drawRing(current_polygon_.outer(), tf.value(), boundaries_node_);
  auto& obstacles = bg::interior_rings(current_polygon_);
  for(auto& obstacle : obstacles){
    drawRing(obstacle, tf.value(), boundaries_node_);
  }
}

void ExactDecomposition::drawRing(mrs_lib::Polygon::ring_type& ring, geometry_msgs::TransformStamped tf, Ogre::SceneNode* node){
  for(int i=0; i<ring.size()-1; i++){
    mrs_lib::Point2d tmp_start = ring[i];
    mrs_lib::Point2d tmp_end = ring[i+1];

    geometry_msgs::Point start;
    start.x = bg::get<0>(tmp_start);
    start.y = bg::get<1>(tmp_start);
    start.z = height_;

    geometry_msgs::Point end;
    end.x = bg::get<0>(tmp_end);
    end.y = bg::get<1>(tmp_end);
    end.z = height_;
    // Transform points to current frame
    const auto& start_transformed = transformer_.transform(start, tf);
    const auto& end_transformed = transformer_.transform(end, tf);
    if (!start_transformed || !end_transformed) {
      ROS_INFO("[ExactDecomposition]: Unable to transform cmd reference from %s to %s at time %.6f.", current_frame_.c_str(), polygon_frame_.c_str(),
              ros::Time::now().toSec());
      continue;
    }
    start = start_transformed.value();
    end = end_transformed.value();

    rviz::Line* line = new rviz::Line(scene_manager_, node);
    line->setColor(1, 0, 0, 1);
    line->setPoints(Ogre::Vector3(start.x, start.y, start.z), Ogre::Vector3(end.x, end.y, end.z));
    line->setPosition(Ogre::Vector3(0, 0, 0));
    line->setScale(Ogre::Vector3(1, 1, 1));
    line->setVisible(true);
  }
}

void ExactDecomposition::drawDecomposition(std::vector<mrs_lib::Polygon::ring_type>& polygons){
  if(decomposition_node_){
    scene_manager_->destroySceneNode(decomposition_node_);
  }
  decomposition_node_ = root_node_->createChildSceneNode();

  const auto& tf = transformer_.getTransform(polygon_frame_, current_frame_);
  if (!tf) {
    ROS_INFO("[ExactDecomposition]: Transformation is not found. boundaries will not be displayed");
    return;
  }
  for(auto& polygon : polygons){
    drawRing(polygon, tf.value(), decomposition_node_);
  }
}

void ExactDecomposition::drawPath(mrs_msgs::PathSrv& path){
  // Delete previous path
  if(path_node_){
    scene_manager_->destroySceneNode(path_node_);
  }
  path_node_ = root_node_->createChildSceneNode();

  // Prepare transformation
  const auto& tf = transformer_.getTransform(path.request.path.header.frame_id, current_frame_);
  if (!tf) {
    ROS_INFO("[ExactDecomposition]: Transformation is not found. Path will not be displayed");
    return;
  }

  for(size_t i=0; i<path.request.path.points.size()-1; ++i){
    geometry_msgs::Point start = path.request.path.points[i].position;
    geometry_msgs::Point end = path.request.path.points[i+1].position;

    // Transform points to current frame
    const auto& start_transformed = transformer_.transform(start, tf.value());
    const auto& end_transformed = transformer_.transform(end, tf.value());
    if (!start_transformed || !end_transformed) {
      ROS_INFO("[ExactDecomposition]: Unable to transform cmd reference from %s to %s at time %.6f.", path.request.path.header.frame_id.c_str(), current_frame_.c_str(),
              ros::Time::now().toSec());
      continue;
    }
    start = start_transformed.value();
    end = end_transformed.value();

    rviz::Line* line = new rviz::Line(scene_manager_, path_node_);
    line->setColor(1, 0, 0, 1);
    line->setPoints(Ogre::Vector3(start.x, start.y, start.z), Ogre::Vector3(end.x, end.y, end.z));
    line->setPosition(Ogre::Vector3(0, 0, 0));
    line->setScale(Ogre::Vector3(1, 1, 1));
    line->setVisible(true);
  }
}

// |----------------------- Public methods -----------------------|

void ExactDecomposition::initialize(rviz::Property* property_container, Ogre::SceneManager* scene_manager, Ogre::SceneNode* root_node) {
  CoverageMethod::initialize(property_container, scene_manager, root_node);

  boundaries_property_    = new rviz::BoolProperty("Show boundaries", true, "Enable to show", property_container, SLOT(boundariesChanged()), this);
  decomposition_property_ = new rviz::BoolProperty("Show decomposition", true, "Enable to show", property_container, SLOT(decompositionChanged()), this);
  path_property_          = new rviz::BoolProperty("Show path", true, "Enable to show", property_container, SLOT(pathChanged()), this);
  turn_num_property_  = new rviz::IntProperty("Turns", 0, "Number of turns in path", property_container);

  turn_num_property_->setReadOnly(true);
}

void ExactDecomposition::setPolygon(std::string frame_id, mrs_lib::Polygon &new_polygon, bool update){
  current_polygon_ = new_polygon;
  polygon_frame_ = frame_id;
  if(update){
    drawCurrentPolygon();
  }
}

void ExactDecomposition::setStart(Ogre::Vector3 position){
  start_position_ = position;
}

void ExactDecomposition::setAngle(int angle, bool update) {
  angle_ = angle;
  if(update){
    drawCurrentPolygon();
  }
}

void ExactDecomposition::setOverlap(float percentage, bool update) {
  overlap_ = percentage;
  if(update){
    drawCurrentPolygon();
  }
}

void ExactDecomposition::setHeight(float height, bool update) {
  height_ = height;
  if(update){
    drawCurrentPolygon();
  }
}

void ExactDecomposition::setFrame(std::string new_frame, bool update){
  current_frame_ = new_frame;
  if(update){
    drawCurrentPolygon();
  }
}

// |--------------------- Slots ---------------------|

void ExactDecomposition::decompositionChanged() {
  decomposition_node_->setVisible(decomposition_property_->getBool());
}

void ExactDecomposition::boundariesChanged() {
  boundaries_node_->setVisible(boundaries_property_->getBool());
}

void ExactDecomposition::pathChanged() {
  path_node_->setVisible(path_property_->getBool());
}

} // namespace mrs_rviz_plugins