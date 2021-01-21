// clang: MatousFormat

#ifndef SPHERE_H
#define SPHERE_H

#include <mrs_msgs/Sphere.h>
#include <OGRE/OgreCamera.h>
#include <OGRE/OgreVector3.h>
#include <OGRE/OgreSceneNode.h>
#include <OGRE/OgreSceneManager.h>
#include <OGRE/OgreManualObject.h>

#include <rviz/message_filter_display.h>
#include <rviz/view_manager.h>
#include <rviz/view_controller.h>

#include <mutex>

namespace Ogre
{
  class Vector3;
  class Quaternion;
}  // namespace Ogre

namespace rviz
{
  class Arrow;
  class MeshShape;
  class Object;
}  // namespace rviz

namespace mrs_rviz_plugins
{

  namespace sphere
  {

    class Visual : public QObject
    {
Q_OBJECT
    public:
      // Constructor.  Creates the visual stuff and puts it into the
      // scene, but in an unconfigured state.
      Visual(rviz::DisplayContext* context, Ogre::SceneNode* parent_node);

      // Destructor.  Removes the visual stuff from the scene.
      virtual ~Visual();

      void setMessage(const mrs_msgs::Sphere::ConstPtr& msg);

      // Set the pose of the coordinate frame the message refers to.
      // These could be done inside setMessage(), but that would require
      // calls to FrameManager and error handling inside setMessage(),
      // which doesn't seem as clean.  This way Visual is only
      // responsible for visualization.
      void setFramePosition(const Ogre::Vector3& position);
      void setFrameOrientation(const Ogre::Quaternion& orientation);

      void setColor(const float red, const float green, const float blue, const float alpha);
      void setDrawDynamic(const bool draw);
      void setDrawStatic(const bool draw);

      static int sphere_idx;

 private Q_SLOTS:
      void onViewControllerChanged();

      void fillCircle(Ogre::ManualObject* circle, float x, float y, float z, float r, const Ogre::Quaternion& q, int n_pts = 32);

      Ogre::ManualObject* initCircle(const std::string& name, float x, float y, float z, float r, const Ogre::Quaternion& q, int n_pts = 32);

      void freeCircle(Ogre::ManualObject*& circle_ptr);

    private:
      bool got_msg_ = false;

      // values set in Rviz
      bool draw_dynamic_ = true;
      bool draw_static_ = true;
      float red_, green_, blue_, alpha_;

      // values received from the message
      double radius_;
      Ogre::Vector3 position_;

      std::mutex circles_quat_mtx_;
      std::array<Ogre::ManualObject*, 4> circles_ = {nullptr};
      std::array<std::string, 4> circle_names_ = {"circle_dyn" + std::to_string(sphere_idx), "circle_xy" + std::to_string(sphere_idx), "circle_yz" + std::to_string(sphere_idx), "circle_xz" + std::to_string(sphere_idx)};
      std::array<Ogre::Quaternion, 4> circle_quats_ = {};
      Ogre::ManualObject*& circle_dyn_ = circles_[0];

      Ogre::Camera* camera_ = nullptr;

      rviz::DisplayContext* context_; // initialized in constructor

      // A SceneNode whose pose is set to match the coordinate frame of
      // the sphere message header.
      Ogre::SceneNode* frame_node_; // initialized in constructor

      // The SceneManager, kept here only so the destructor can ask it to
      // destroy the ``frame_node_``.
      Ogre::SceneManager* scene_manager_; // initialized in constructor

      class CameraListener : public Ogre::Camera::Listener
      {
        Visual* vis_;
        Ogre::ManualObject*& circle_;

        public:
        CameraListener(Visual* vis)
          : vis_(vis), circle_(vis_->circle_dyn_)
        {}

        float last_x, last_y, last_cx, last_cy;
        void cameraPreRenderScene(Ogre::Camera *cam)
        {
          /* const Ogre::Vector3 eyeSpacePos = cam->getViewMatrix(true) * vis_->position_; */
          /* const auto projMat = cam->getProjectionMatrix(); */
          std::lock_guard<std::mutex> lck(vis_->circles_quat_mtx_);
          Ogre::Quaternion& q = vis_->circle_quats_.at(0);
          q = vis_->frame_node_->convertWorldToLocalOrientation(cam->getOrientation());
          circle_->beginUpdate(0);
          vis_->fillCircle(circle_, vis_->position_.x, vis_->position_.y, vis_->position_.z, vis_->radius_, q);
          circle_->end();
        }
      };

      CameraListener* cam_listener_ = new CameraListener(this);
    };

  }  // namespace sphere

}  // end namespace mrs_rviz_plugins

#endif // SPHERE_H
