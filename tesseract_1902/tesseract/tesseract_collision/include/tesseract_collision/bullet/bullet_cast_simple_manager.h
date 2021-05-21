/**
 * @file bullet_cast_simple_manager.h
 * @brief Tesseract ROS Bullet cast(continuous) simple collision manager.
 *
 * @author Levi Armstrong
 * @date Dec 18, 2017
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2017, Southwest Research Institute
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

#include <tesseract_collision/bullet/bullet_utils.h>
#include <tesseract_core/continuous_contact_manager_base.h>

#ifndef TESSERACT_COLLISION_BULLET_CAST_SIMPLE_MANAGERS_H
#define TESSERACT_COLLISION_BULLET_CAST_SIMPLE_MANAGERS_H

namespace tesseract
{
namespace tesseract_bullet
{
/** @brief A simple implementaiton of a tesseract manager which does not use BHV */
class BulletCastSimpleManager : public ContinuousContactManagerBase
{
public:
  BulletCastSimpleManager();

  ContinuousContactManagerBasePtr clone() const override;

  bool addCollisionObject(const std::string& name,
                          const int& mask_id,
                          const std::vector<shapes::ShapeConstPtr>& shapes,
                          const VectorIsometry3d& shape_poses,
                          const CollisionObjectTypeVector& collision_object_types,
                          bool enabled = true) override;

  bool hasCollisionObject(const std::string& name) const override;

  bool removeCollisionObject(const std::string& name) override;

  bool enableCollisionObject(const std::string& name) override;

  bool disableCollisionObject(const std::string& name) override;

  void setCollisionObjectsTransform(const std::string& name, const Eigen::Isometry3d& pose) override;

  void setCollisionObjectsTransform(const std::vector<std::string>& names, const VectorIsometry3d& poses) override;

  void setCollisionObjectsTransform(const TransformMap& transforms) override;

  void setCollisionObjectsTransform(const std::string& name,
                                    const Eigen::Isometry3d& pose1,
                                    const Eigen::Isometry3d& pose2) override;

  void setCollisionObjectsTransform(const std::vector<std::string>& names,
                                    const VectorIsometry3d& pose1,
                                    const VectorIsometry3d& pose2) override;

  void setCollisionObjectsTransform(const TransformMap& pose1, const TransformMap& pose2) override;

  void setActiveCollisionObjects(const std::vector<std::string>& names) override;

  const std::vector<std::string>& getActiveCollisionObjects() const override;

  void setContactDistanceThreshold(double contact_distance) override;

  double getContactDistanceThreshold() const override;

  void setIsContactAllowedFn(IsContactAllowedFn fn) override;

  IsContactAllowedFn getIsContactAllowedFn() const override;

  void contactTest(ContactResultMap& collisions, const ContactTestType& type) override;

  /**
   * @brief A a bullet collision object to the manager
   * @param cow The tesseract bullet collision object
   */
  void addCollisionObject(const COWPtr& cow);

private:
  std::vector<std::string> active_; /**< @brief A list of the active collision objects */
  double contact_distance_;         /**< @brief The contact distance threshold */
  IsContactAllowedFn fn_;           /**< @brief The is allowed collision function */

  std::unique_ptr<btCollisionDispatcher> dispatcher_; /**< @brief The bullet collision dispatcher used for getting
                                                         object to object collison algorithm */
  btDispatcherInfo dispatch_info_;              /**< @brief The bullet collision dispatcher configuration information */
  btDefaultCollisionConfiguration coll_config_; /**< @brief The bullet collision configuration */
  Link2Cow link2cow_;                           /**< @brief A map of collision objects being managed */
  std::vector<COWPtr> cows_;                    /**< @brief A vector of collision objects (active followed by static) */
  Link2Cow link2castcow_;                       /**< @brief A map of cast collision objects being managed. */
};
typedef std::shared_ptr<BulletCastSimpleManager> BulletCastSimpleManagerPtr;
}
}

#endif  // TESSERACT_COLLISION_BULLET_CAST_SIMPLE_MANAGERS_H
