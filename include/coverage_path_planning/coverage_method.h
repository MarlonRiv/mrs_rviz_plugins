#ifndef COVERAGE_PATH_PLANNING_METHOD_H
#define COVERAGE_PATH_PLANNING_METHOD_H

#include <mrs_lib/safety_zone/polygon.h>
#include <rviz/properties/property.h>
#include <OGRE/OgreSceneManager.h>

namespace mrs_rviz_plugins{

class CoverageMethod{

public:
  // CoverageMethod(rviz::Property* property_container, Ogre::SceneManager* scene_manager){
  //   property_container_ = property_container;
  //   scene_manager_ = scene_manager;
  // }

  void initialize(rviz::Property* property_container, Ogre::SceneManager* scene_manager){
    property_container_ = property_container;
    scene_manager_ = scene_manager;
  }

  virtual void update(mrs_lib::Polygon &new_polygon) = 0;

  virtual void compute(mrs_lib::Polygon &new_polygon) = 0;

  virtual void start() = 0;

protected:
  rviz::Property* property_container_;
  Ogre::SceneManager* scene_manager_;

}; // class CoverageMethod

} // namespace mrs_rviz_plugins

#endif