#ifndef TESSERACT_ROS_PLANNING_CONTINUOUS_MOTION_VALIDATOR_H
#define TESSERACT_ROS_PLANNING_CONTINUOUS_MOTION_VALIDATOR_H

#include <tesseract_core/macros.h>
TESSERACT_IGNORE_WARNINGS_PUSH
#include <ompl/base/DiscreteMotionValidator.h>
#include <ompl/base/MotionValidator.h>
TESSERACT_IGNORE_WARNINGS_POP

#include <tesseract_ros/kdl/kdl_env.h>

namespace tesseract
{
namespace tesseract_planning
{
class ContinuousMotionValidator : public ompl::base::MotionValidator
{
public:
  ContinuousMotionValidator(ompl::base::SpaceInformationPtr space_info,
                            tesseract::BasicEnvConstPtr env,
                            const std::string& manipulator);

  bool checkMotion(const ompl::base::State* s1, const ompl::base::State* s2) const override;

  bool checkMotion(const ompl::base::State* s1,
                   const ompl::base::State* s2,
                   std::pair<ompl::base::State*, double>& lastValid) const override;

private:
  bool continuousCollisionCheck(const ompl::base::State* s1, const ompl::base::State* s2) const;

  bool isContactAllowed(const std::string& a, const std::string& b) const
  {
    return env_->getAllowedCollisionMatrix()->isCollisionAllowed(a, b);
  }

  tesseract::BasicEnvConstPtr env_;
  tesseract::ContinuousContactManagerBasePtr contact_manager_;
  tesseract::IsContactAllowedFn is_allowed_cb_;
  std::vector<std::string> links_;
  std::vector<std::string> joints_;
};
}
}

#endif  // TESSERACT_ROS_PLANNING_CONTINUOUS_MOTION_VALIDATOR_H
