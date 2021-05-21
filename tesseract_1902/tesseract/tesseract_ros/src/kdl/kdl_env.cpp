/**
 * @file bullet_env.cpp
 * @brief Tesseract ROS Bullet environment implementation.
 *
 * @author John Schulman
 * @author Levi Armstrong
 * @date Dec 18, 2017
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2017, Southwest Research Institute
 * @copyright Copyright (c) 2013, John Schulman
 *
 * @par License
 * Software License Agreement (BSD-2-Clause)
 * @par
 * All rights reserved.
 * @par
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * @par
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * @par
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <tesseract_core/macros.h>
TESSERACT_IGNORE_WARNINGS_PUSH
#include <eigen_conversions/eigen_msg.h>
#include <functional>
#include <geometric_shapes/shape_operations.h>
#include <iostream>
#include <limits>
#include <octomap/octomap.h>
TESSERACT_IGNORE_WARNINGS_POP

#include "tesseract_ros/kdl/kdl_env.h"
#include "tesseract_ros/kdl/kdl_chain_kin.h"
#include "tesseract_ros/kdl/kdl_joint_kin.h"
#include "tesseract_ros/kdl/kdl_utils.h"
#include "tesseract_ros/ros_tesseract_utils.h"

const std::string DEFAULT_DISCRETE_CONTACT_MANAGER_PLUGIN_PARAM = "tesseract_collision/BulletDiscreteBVHManager";
const std::string DEFAULT_CONTINUOUS_CONTACT_MANAGER_PLUGIN_PARAM = "tesseract_collision/BulletCastBVHManager";

namespace tesseract
{
namespace tesseract_ros
{
using Eigen::MatrixXd;
using Eigen::VectorXd;

bool KDLEnv::init(urdf::ModelInterfaceConstSharedPtr urdf_model) { return init(urdf_model, nullptr); }
bool KDLEnv::init(urdf::ModelInterfaceConstSharedPtr urdf_model, srdf::ModelConstSharedPtr srdf_model)
{
  initialized_ = false;
  urdf_model_ = urdf_model;
  object_colors_ = ObjectColorMapPtr(new ObjectColorMap());

  if (urdf_model_ == nullptr)
  {
    ROS_ERROR_STREAM("Null pointer to URDF Model");
    return initialized_;
  }

  if (!urdf_model_->getRoot())
  {
    ROS_ERROR("Invalid URDF in ROSKDLEnv::init call");
    return initialized_;
  }

  KDL::Tree* kdl_tree = new KDL::Tree();
  if (!kdl_parser::treeFromUrdfModel(*urdf_model_, *kdl_tree))
  {
    ROS_ERROR("Failed to initialize KDL from URDF model");
    return initialized_;
  }
  kdl_tree_ = std::shared_ptr<KDL::Tree>(kdl_tree);
  initialized_ = true;

  if (initialized_)
  {
    link_names_.reserve(urdf_model->links_.size());
    for (auto& link : urdf_model->links_)
      link_names_.push_back(link.second->name);

    current_state_ = EnvStatePtr(new EnvState());
    kdl_jnt_array_.resize(kdl_tree_->getNrOfJoints());
    joint_names_.resize(kdl_tree_->getNrOfJoints());
    size_t j = 0;
    for (const auto& seg : kdl_tree_->getSegments())
    {
      const KDL::Joint& jnt = seg.second.segment.getJoint();

      if (jnt.getType() == KDL::Joint::None)
        continue;
      joint_names_[j] = jnt.getName();
      joint_to_qnr_.insert(std::make_pair(jnt.getName(), seg.second.q_nr));
      kdl_jnt_array_(seg.second.q_nr) = 0.0;
      current_state_->joints.insert(std::make_pair(jnt.getName(), 0.0));

      j++;
    }

    calculateTransforms(
        current_state_->transforms, kdl_jnt_array_, kdl_tree_->getRootSegment(), Eigen::Isometry3d::Identity());
  }

  if (srdf_model != nullptr)
  {
    srdf_model_ = srdf_model;
    for (const auto& group : srdf_model_->getGroups())
    {
      if (!group.chains_.empty())
      {
        assert(group.chains_.size() == 1);
        addManipulator(group.chains_.front().first, group.chains_.front().second, group.name_);
      }

      if (!group.joints_.empty())
      {
        addManipulator(group.joints_, group.name_);
      }

      // TODO: Need to add other options
      if (!group.links_.empty())
      {
        ROS_ERROR("Link groups are currently not supported!");
      }

      if (!group.subgroups_.empty())
      {
        ROS_ERROR("Subgroups are currently not supported!");
      }
    }

    // Populate allowed collision matrix
    for (const auto& pair : srdf_model_->getDisabledCollisionPairs())
    {
      allowed_collision_matrix_->addAllowedCollision(pair.link1_, pair.link2_, pair.reason_);
    }
  }

  // Now get the active link names
  getActiveLinkNamesRecursive(active_link_names_, urdf_model_->getRoot(), false);

  // load the default contact checker plugin
  loadDiscreteContactManagerPlugin(DEFAULT_DISCRETE_CONTACT_MANAGER_PLUGIN_PARAM);
  loadContinuousContactManagerPlugin(DEFAULT_CONTINUOUS_CONTACT_MANAGER_PLUGIN_PARAM);

  discrete_manager_->setActiveCollisionObjects(active_link_names_);
  continuous_manager_->setActiveCollisionObjects(active_link_names_);

  return initialized_;
}

void KDLEnv::setState(const std::unordered_map<std::string, double>& joints)
{
  current_state_->joints.insert(joints.begin(), joints.end());

  for (auto& joint : joints)
  {
    if (setJointValuesHelper(kdl_jnt_array_, joint.first, joint.second))
    {
      current_state_->joints[joint.first] = joint.second;
    }
  }

  calculateTransforms(
      current_state_->transforms, kdl_jnt_array_, kdl_tree_->getRootSegment(), Eigen::Isometry3d::Identity());
  discrete_manager_->setCollisionObjectsTransform(current_state_->transforms);
  continuous_manager_->setCollisionObjectsTransform(current_state_->transforms);
}

void KDLEnv::setState(const std::vector<std::string>& joint_names, const std::vector<double>& joint_values)
{
  for (auto i = 0u; i < joint_names.size(); ++i)
  {
    if (setJointValuesHelper(kdl_jnt_array_, joint_names[i], joint_values[i]))
    {
      current_state_->joints[joint_names[i]] = joint_values[i];
    }
  }

  calculateTransforms(
      current_state_->transforms, kdl_jnt_array_, kdl_tree_->getRootSegment(), Eigen::Isometry3d::Identity());
  discrete_manager_->setCollisionObjectsTransform(current_state_->transforms);
  continuous_manager_->setCollisionObjectsTransform(current_state_->transforms);
}

void KDLEnv::setState(const std::vector<std::string>& joint_names,
                      const Eigen::Ref<const Eigen::VectorXd>& joint_values)
{
  for (auto i = 0u; i < joint_names.size(); ++i)
  {
    if (setJointValuesHelper(kdl_jnt_array_, joint_names[i], joint_values[i]))
    {
      current_state_->joints[joint_names[i]] = joint_values[i];
    }
  }

  calculateTransforms(
      current_state_->transforms, kdl_jnt_array_, kdl_tree_->getRootSegment(), Eigen::Isometry3d::Identity());
  discrete_manager_->setCollisionObjectsTransform(current_state_->transforms);
  for (const auto& tf : current_state_->transforms)
  {
    if (std::find(active_link_names_.begin(), active_link_names_.end(), tf.first) != active_link_names_.end())
    {
      continuous_manager_->setCollisionObjectsTransform(tf.first, tf.second, tf.second);
    }
    else
    {
      continuous_manager_->setCollisionObjectsTransform(tf.first, tf.second);
    }
  }
}

EnvStatePtr KDLEnv::getState(const std::unordered_map<std::string, double>& joints) const
{
  EnvStatePtr state(new EnvState(*current_state_));
  KDL::JntArray jnt_array = kdl_jnt_array_;

  for (auto& joint : joints)
  {
    if (setJointValuesHelper(jnt_array, joint.first, joint.second))
    {
      state->joints[joint.first] = joint.second;
    }
  }

  calculateTransforms(state->transforms, jnt_array, kdl_tree_->getRootSegment(), Eigen::Isometry3d::Identity());

  return state;
}

EnvStatePtr KDLEnv::getState(const std::vector<std::string>& joint_names, const std::vector<double>& joint_values) const
{
  EnvStatePtr state(new EnvState(*current_state_));
  KDL::JntArray jnt_array = kdl_jnt_array_;

  for (auto i = 0u; i < joint_names.size(); ++i)
  {
    if (setJointValuesHelper(jnt_array, joint_names[i], joint_values[i]))
    {
      state->joints[joint_names[i]] = joint_values[i];
    }
  }

  calculateTransforms(state->transforms, jnt_array, kdl_tree_->getRootSegment(), Eigen::Isometry3d::Identity());

  return state;
}

EnvStatePtr KDLEnv::getState(const std::vector<std::string>& joint_names,
                             const Eigen::Ref<const Eigen::VectorXd>& joint_values) const
{
  EnvStatePtr state(new EnvState(*current_state_));
  KDL::JntArray jnt_array = kdl_jnt_array_;

  for (auto i = 0u; i < joint_names.size(); ++i)
  {
    if (setJointValuesHelper(jnt_array, joint_names[i], joint_values[i]))
    {
      state->joints[joint_names[i]] = joint_values[i];
    }
  }

  calculateTransforms(state->transforms, jnt_array, kdl_tree_->getRootSegment(), Eigen::Isometry3d::Identity());

  return state;
}

Eigen::VectorXd KDLEnv::getCurrentJointValues() const
{
  Eigen::VectorXd jv;
  jv.resize(static_cast<long int>(joint_names_.size()));
  for (auto j = 0u; j < joint_names_.size(); ++j)
  {
    jv(j) = current_state_->joints[joint_names_[j]];
  }
  return jv;
}

Eigen::VectorXd KDLEnv::getCurrentJointValues(const std::string& manipulator_name) const
{
  auto it = manipulators_.find(manipulator_name);
  if (it != manipulators_.end())
  {
    const std::vector<std::string>& joint_names = it->second->getJointNames();
    Eigen::VectorXd start_pos(joint_names.size());

    for (auto j = 0u; j < joint_names.size(); ++j)
    {
      start_pos(j) = current_state_->joints[joint_names[j]];
    }

    return start_pos;
  }

  return Eigen::VectorXd();
}

VectorIsometry3d KDLEnv::getLinkTransforms() const
{
  VectorIsometry3d link_tfs;
  link_tfs.resize(link_names_.size());
  for (const auto& link_name : link_names_)
  {
    link_tfs.push_back(current_state_->transforms[link_name]);
  }
  return link_tfs;
}

const Eigen::Isometry3d& KDLEnv::getLinkTransform(const std::string& link_name) const
{
  return current_state_->transforms[link_name];
}

bool KDLEnv::addManipulator(const std::string& base_link,
                            const std::string& tip_link,
                            const std::string& manipulator_name)
{
  if (!hasManipulator(manipulator_name))
  {
    KDLChainKinPtr manip(new KDLChainKin());
    if (!manip->init(urdf_model_, base_link, tip_link, manipulator_name))
    {
      return false;
    }

    manipulators_.insert(std::make_pair(manipulator_name, manip));
    return true;
  }
  return false;
}

bool KDLEnv::addManipulator(const std::vector<std::string>& joint_names, const std::string& manipulator_name)
{
  if (!hasManipulator(manipulator_name))
  {
    KDLJointKinPtr manip(new KDLJointKin());
    if (!manip->init(urdf_model_, joint_names, manipulator_name))
    {
      return false;
    }

    manipulators_.insert(std::make_pair(manipulator_name, manip));
    return true;
  }
  return false;
}

bool KDLEnv::hasManipulator(const std::string& manipulator_name) const
{
  return manipulators_.find(manipulator_name) != manipulators_.end();
}

BasicKinConstPtr KDLEnv::getManipulator(const std::string& manipulator_name) const
{
  auto it = manipulators_.find(manipulator_name);
  if (it != manipulators_.end())
    return it->second;

  return nullptr;
}

std::string KDLEnv::getManipulatorName(const std::vector<std::string>& joint_names) const
{
  std::set<std::string> joint_names_set(joint_names.begin(), joint_names.end());
  for (const auto& manip : manipulators_)
  {
    const std::vector<std::string>& tmp_joint_names = manip.second->getJointNames();
    std::set<std::string> tmp_joint_names_set(tmp_joint_names.begin(), tmp_joint_names.end());
    if (joint_names_set == tmp_joint_names_set)
      return manip.first;
  }
  return "";
}

void KDLEnv::addAttachableObject(const AttachableObjectConstPtr attachable_object)
{
  const auto object = attachable_objects_.find(attachable_object->name);
  if (object != attachable_objects_.end())
  {
    discrete_manager_->removeCollisionObject(attachable_object->name);
    continuous_manager_->removeCollisionObject(attachable_object->name);
    ROS_DEBUG("Replacing attachable object %s!", attachable_object->name.c_str());
  }

  attachable_objects_[attachable_object->name] = attachable_object;

  // Add the object to the contact checker
  discrete_manager_->addCollisionObject(attachable_object->name,
                                        BodyTypes::ROBOT_ATTACHED,
                                        attachable_object->collision.shapes,
                                        attachable_object->collision.shape_poses,
                                        attachable_object->collision.collision_object_types,
                                        false);

  continuous_manager_->addCollisionObject(attachable_object->name,
                                          BodyTypes::ROBOT_ATTACHED,
                                          attachable_object->collision.shapes,
                                          attachable_object->collision.shape_poses,
                                          attachable_object->collision.collision_object_types,
                                          false);
}

void KDLEnv::removeAttachableObject(const std::string& name)
{
  if (attachable_objects_.find(name) != attachable_objects_.end())
  {
    attachable_objects_.erase(name);
    discrete_manager_->removeCollisionObject(name);
    continuous_manager_->removeCollisionObject(name);
  }
}

void KDLEnv::clearAttachableObjects()
{
  for (const auto& obj : attachable_objects_)
  {
    discrete_manager_->removeCollisionObject(obj.first);
    continuous_manager_->removeCollisionObject(obj.first);
  }
  attachable_objects_.clear();
}

const AttachedBodyInfo& KDLEnv::getAttachedBody(const std::string& name) const
{
  const auto body = attached_bodies_.find(name);
  if (body == attached_bodies_.end())
    ROS_ERROR("Tried to get attached body %s which does not exist!", name.c_str());

  return body->second;
}

void KDLEnv::attachBody(const AttachedBodyInfo& attached_body_info)
{
  const auto obj = attachable_objects_.find(attached_body_info.object_name);
  const auto body_info = attached_bodies_.find(attached_body_info.object_name);

  if (body_info != attached_bodies_.end())
  {
    ROS_DEBUG("Tried to attached object %s which is already attached!", attached_body_info.object_name.c_str());
    return;
  }

  if (obj == attachable_objects_.end())
  {
    ROS_DEBUG("Tried to attached object %s which does not exist!", attached_body_info.object_name.c_str());
    return;
  }

  if (std::find(link_names_.begin(), link_names_.end(), attached_body_info.object_name) != link_names_.end())
  {
    ROS_DEBUG("Tried to attached object %s with the same name as an existing link!",
              attached_body_info.object_name.c_str());
    return;
  }

  link_names_.push_back(attached_body_info.object_name);
  if (std::find(active_link_names_.begin(), active_link_names_.end(), attached_body_info.parent_link_name) !=
      active_link_names_.end())
  {
    active_link_names_.push_back(attached_body_info.object_name);
  }

  attached_bodies_.insert(std::make_pair(attached_body_info.object_name, attached_body_info));
  discrete_manager_->enableCollisionObject(attached_body_info.object_name);
  continuous_manager_->enableCollisionObject(attached_body_info.object_name);

  // update attached object's transform
  calculateTransforms(
      current_state_->transforms, kdl_jnt_array_, kdl_tree_->getRootSegment(), Eigen::Isometry3d::Identity());

  discrete_manager_->setCollisionObjectsTransform(attached_body_info.object_name,
                                                  current_state_->transforms[attached_body_info.object_name]);
  continuous_manager_->setCollisionObjectsTransform(attached_body_info.object_name,
                                                    current_state_->transforms[attached_body_info.object_name]);

  discrete_manager_->setActiveCollisionObjects(active_link_names_);
  continuous_manager_->setActiveCollisionObjects(active_link_names_);

  // Update manipulators
  for (auto& manip : manipulators_)
  {
    const auto& manip_link_names = manip.second->getLinkNames();
    if (std::find(manip_link_names.begin(), manip_link_names.end(), attached_body_info.parent_link_name) !=
        manip_link_names.end())
    {
      manip.second->addAttachedLink(attached_body_info.object_name, attached_body_info.parent_link_name);
    }
  }
}

void KDLEnv::detachBody(const std::string& name)
{
  if (attached_bodies_.find(name) != attached_bodies_.end())
  {
    attached_bodies_.erase(name);
    link_names_.erase(std::remove(link_names_.begin(), link_names_.end(), name), link_names_.end());
    active_link_names_.erase(std::remove(active_link_names_.begin(), active_link_names_.end(), name),
                             active_link_names_.end());
    discrete_manager_->setActiveCollisionObjects(active_link_names_);
    continuous_manager_->setActiveCollisionObjects(active_link_names_);
    discrete_manager_->disableCollisionObject(name);
    continuous_manager_->disableCollisionObject(name);
    current_state_->transforms.erase(name);

    // Update manipulators
    for (auto& manip : manipulators_)
    {
      const auto& manip_link_names = manip.second->getLinkNames();
      if (std::find(manip_link_names.begin(), manip_link_names.end(), name) != manip_link_names.end())
      {
        manip.second->removeAttachedLink(name);
      }
    }
  }
}

void KDLEnv::clearAttachedBodies()
{
  for (const auto& body : attached_bodies_)
  {
    std::string name = body.second.object_name;
    discrete_manager_->disableCollisionObject(name);
    continuous_manager_->disableCollisionObject(name);
    link_names_.erase(std::remove(link_names_.begin(), link_names_.end(), name), link_names_.end());
    active_link_names_.erase(std::remove(active_link_names_.begin(), active_link_names_.end(), name),
                             active_link_names_.end());
    current_state_->transforms.erase(name);
  }
  attached_bodies_.clear();
  discrete_manager_->setActiveCollisionObjects(active_link_names_);
  continuous_manager_->setActiveCollisionObjects(active_link_names_);

  // Update manipulators
  for (auto& manip : manipulators_)
    manip.second->clearAttachedLinks();
}

bool KDLEnv::setJointValuesHelper(KDL::JntArray& q, const std::string& joint_name, const double& joint_value) const
{
  auto qnr = joint_to_qnr_.find(joint_name);
  if (qnr != joint_to_qnr_.end())
  {
    q(qnr->second) = joint_value;
    return true;
  }
  else
  {
    ROS_ERROR("Tried to set joint name %s which does not exist!", joint_name.c_str());
    return false;
  }
}

void KDLEnv::calculateTransformsHelper(TransformMap& transforms,
                                       const KDL::JntArray& q_in,
                                       const KDL::SegmentMap::const_iterator& it,
                                       const Eigen::Isometry3d& parent_frame) const
{
  if (it != kdl_tree_->getSegments().end())
  {
    const KDL::TreeElementType& current_element = it->second;
    KDL::Frame current_frame = GetTreeElementSegment(current_element).pose(q_in(GetTreeElementQNr(current_element)));

    Eigen::Isometry3d local_frame, global_frame;
    KDLToEigen(current_frame, local_frame);
    global_frame = parent_frame * local_frame;
    transforms[current_element.segment.getName()] = global_frame;

    for (auto& child : current_element.children)
    {
      calculateTransformsHelper(transforms, q_in, child, global_frame);
    }
  }
}

void KDLEnv::calculateTransforms(TransformMap& transforms,
                                 const KDL::JntArray& q_in,
                                 const KDL::SegmentMap::const_iterator& it,
                                 const Eigen::Isometry3d& parent_frame) const
{
  calculateTransformsHelper(transforms, q_in, it, parent_frame);

  // update attached objects location
  for (const auto& attached : attached_bodies_)
  {
    transforms[attached.first] = transforms[attached.second.parent_link_name] * attached.second.transform;
  }
}

bool KDLEnv::defaultIsContactAllowedFn(const std::string& link_name1, const std::string& link_name2) const
{
  if (allowed_collision_matrix_ != nullptr && allowed_collision_matrix_->isCollisionAllowed(link_name1, link_name2))
    return true;

  auto it1 = attached_bodies_.find(link_name1);
  auto it2 = attached_bodies_.find(link_name2);
  if (it1 == attached_bodies_.end() && it2 == attached_bodies_.end())
  {
    return false;
  }
  else if (it1 == attached_bodies_.end())
  {
    const std::vector<std::string>& tl = it2->second.touch_links;
    if (std::find(tl.begin(), tl.end(), link_name1) != tl.end() || link_name1 == it2->second.parent_link_name)
      return true;
  }
  else
  {
    const std::vector<std::string>& tl = it1->second.touch_links;
    if (std::find(tl.begin(), tl.end(), link_name2) != tl.end() || link_name2 == it1->second.parent_link_name)
      return true;
  }

  return false;
}

void KDLEnv::loadDiscreteContactManagerPlugin(const std::string& plugin)
{
  DiscreteContactManagerBasePtr temp = discrete_manager_loader_->createUniqueInstance(plugin);
  if (temp != nullptr)
  {
    discrete_manager_ = temp;
  }
  else
  {
    ROS_ERROR("Failed to load tesseract contact checker plugin: %s.", plugin.c_str());
    return;
  }

  discrete_manager_->setIsContactAllowedFn(is_contact_allowed_fn_);
  if (initialized_)
  {
    for (const auto& link : urdf_model_->links_)
    {
      if (link.second->collision_array.size() > 0)
      {
        const std::vector<urdf::CollisionSharedPtr>& col_array =
            link.second->collision_array.empty() ? std::vector<urdf::CollisionSharedPtr>(1, link.second->collision) :
                                                   link.second->collision_array;

        std::vector<shapes::ShapeConstPtr> shapes;
        VectorIsometry3d shape_poses;
        CollisionObjectTypeVector collision_object_types;

        for (std::size_t i = 0; i < col_array.size(); ++i)
        {
          if (col_array[i] && col_array[i]->geometry)
          {
            shapes::ShapeConstPtr s = constructShape(col_array[i]->geometry.get());
            if (s)
            {
              shapes.push_back(s);
              shape_poses.push_back(urdfPose2Eigen(col_array[i]->origin));

              // TODO: Need to encode this in the srdf
              if (s->type == shapes::MESH)
                collision_object_types.push_back(CollisionObjectType::ConvexHull);
              else
                collision_object_types.push_back(CollisionObjectType::UseShapeType);
            }
          }
        }
        discrete_manager_->addCollisionObject(
            link.second->name, BodyType::ROBOT_LINK, shapes, shape_poses, collision_object_types, true);
      }
    }

    // Add attachable collision object to the contact checker in a disabled
    // state
    for (const auto& ao : attachable_objects_)
      discrete_manager_->addCollisionObject(ao.second->name,
                                            BodyTypes::ROBOT_ATTACHED,
                                            ao.second->collision.shapes,
                                            ao.second->collision.shape_poses,
                                            ao.second->collision.collision_object_types,
                                            false);

    // Enable the attached objects in the contact checker
    for (const auto& ab : attached_bodies_)
      discrete_manager_->enableCollisionObject(ab.second.object_name);
  }
}

void KDLEnv::loadContinuousContactManagerPlugin(const std::string& plugin)
{
  ContinuousContactManagerBasePtr temp = continuous_manager_loader_->createUniqueInstance(plugin);
  if (temp != nullptr)
  {
    continuous_manager_ = temp;
  }
  else
  {
    ROS_ERROR("Failed to load tesseract continuous contact manager plugin: %s.", plugin.c_str());
    return;
  }

  continuous_manager_->setIsContactAllowedFn(is_contact_allowed_fn_);
  if (initialized_)
  {
    for (const auto& link : urdf_model_->links_)
    {
      if (link.second->collision_array.size() > 0)
      {
        const std::vector<urdf::CollisionSharedPtr>& col_array =
            link.second->collision_array.empty() ? std::vector<urdf::CollisionSharedPtr>(1, link.second->collision) :
                                                   link.second->collision_array;

        std::vector<shapes::ShapeConstPtr> shapes;
        VectorIsometry3d shape_poses;
        CollisionObjectTypeVector collision_object_types;

        for (std::size_t i = 0; i < col_array.size(); ++i)
        {
          if (col_array[i] && col_array[i]->geometry)
          {
            shapes::ShapeConstPtr s = constructShape(col_array[i]->geometry.get());
            if (s)
            {
              shapes.push_back(s);
              shape_poses.push_back(urdfPose2Eigen(col_array[i]->origin));

              // TODO: Need to encode this in the srdf
              if (s->type == shapes::MESH)
                collision_object_types.push_back(CollisionObjectType::ConvexHull);
              else
                collision_object_types.push_back(CollisionObjectType::UseShapeType);
            }
          }
        }

        continuous_manager_->addCollisionObject(
            link.second->name, BodyType::ROBOT_LINK, shapes, shape_poses, collision_object_types, true);
      }
    }

    // Add attachable collision object to the contact checker in a disabled
    // state
    for (const auto& ao : attachable_objects_)
      continuous_manager_->addCollisionObject(ao.second->name,
                                              BodyTypes::ROBOT_ATTACHED,
                                              ao.second->collision.shapes,
                                              ao.second->collision.shape_poses,
                                              ao.second->collision.collision_object_types,
                                              false);

    // Enable the attached objects in the contact checker
    for (const auto& ab : attached_bodies_)
      continuous_manager_->enableCollisionObject(ab.second.object_name);
  }
}
}
}
