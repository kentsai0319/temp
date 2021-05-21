/**
 * @file contact_checker_common.h
 * @brief This is a collection of common methods
 *
 * @author Levi Armstrong
 * @date Dec 18, 2017
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2017, Southwest Research Institute
 *
 * @par License
 * Software License Agreement (Apache License)
 * @par
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * @par
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef TESSERACT_COLLISION_CONTACT_CHECKER_COMMON_H
#define TESSERACT_COLLISION_CONTACT_CHECKER_COMMON_H

#include <tesseract_core/macros.h>
TESSERACT_IGNORE_WARNINGS_PUSH
#include <LinearMath/btConvexHullComputer.h>
#include <cstdio>
#include <Eigen/Geometry>
#include <fstream>
TESSERACT_IGNORE_WARNINGS_POP

#include <tesseract_core/basic_types.h>

namespace tesseract
{
typedef std::pair<std::string, std::string> ObjectPairKey;

/**
 * @brief Get a key for two object to search the collision matrix
 * @param obj1 First collision object name
 * @param obj2 Second collision object name
 * @return The collision pair key
 */
inline ObjectPairKey getObjectPairKey(const std::string& obj1, const std::string& obj2)
{
  return obj1 < obj2 ? std::make_pair(obj1, obj2) : std::make_pair(obj2, obj1);
}

/**
 * @brief This will check if a link is active provided a list. If the list is empty the link is considered active.
 * @param active List of active link names
 * @param name The name of link to check if it is active.
 */
inline bool isLinkActive(const std::vector<std::string>& active, const std::string& name)
{
  return active.empty() || (std::find(active.begin(), active.end(), name) != active.end());
}

/**
 * @brief Determine if contact is allowed between two objects.
 * @param name1 The name of the first object
 * @param name2 The name of the second object
 * @param acm The contact allowed function
 * @param verbose If true print debug informaton
 * @return True if contact is allowed between the two object, otherwise false.
 */
inline bool
isContactAllowed(const std::string& name1, const std::string& name2, const IsContactAllowedFn acm, bool verbose = false)
{
  // do not distance check geoms part of the same object / link / attached body
  if (name1 == name2)
    return true;

  if (acm != nullptr && acm(name1, name2))
  {
    if (verbose)
    {
      ROS_DEBUG("Collision between '%s' and '%s' is allowed. No contacts are computed.", name1.c_str(), name2.c_str());
    }
    return true;
  }

  if (verbose)
  {
    ROS_DEBUG("Actually checking collisions between %s and %s", name1.c_str(), name2.c_str());
  }

  return false;
}

inline ContactResult* processResult(ContactTestData& cdata,
                                    ContactResult& contact,
                                    const std::pair<std::string, std::string>& key,
                                    bool found)
{
  if (!found)
  {
    ContactResultVector data;
    if (cdata.type == ContactTestType::FIRST)
    {
      data.emplace_back(contact);
      cdata.done = true;
    }
    else
    {
      data.reserve(100);  // TODO: Need better way to initialize this
      data.emplace_back(contact);
    }

    return &(cdata.res.insert(std::make_pair(key, data)).first->second.back());
  }
  else
  {
    assert(cdata.type != ContactTestType::FIRST);
    ContactResultVector& dr = cdata.res[key];
    if (cdata.type == ContactTestType::ALL)
    {
      dr.emplace_back(contact);
      return &(dr.back());
    }
    else if (cdata.type == ContactTestType::CLOSEST)
    {
      if (contact.distance < dr[0].distance)
      {
        dr[0] = contact;
        return &(dr[0]);
      }
    }
    //    else if (cdata.cdata.condition == DistanceRequestType::LIMITED)
    //    {
    //      assert(dr.size() < cdata.req->max_contacts_per_body);
    //      dr.emplace_back(contact);
    //      return &(dr.back());
    //    }
  }

  return nullptr;
}

/**
 * @brief Create a convex hull from vertices using Bullet Convex Hull Computer
 * @param (Output) vertices A vector of vertices
 * @param (Output) faces The first values indicates the number of vertices that define the face followed by the vertice
 * index
 * @param (input) input A vector of point to create a convex hull from
 * @param (input) shrink If positive, the convex hull is shrunken by that amount (each face is moved by "shrink" length
 *                units towards the center along its normal).
 * @param (input) shrinkClamp If positive, "shrink" is clamped to not exceed "shrinkClamp * innerRadius", where
 *                "innerRadius" is the minimum distance of a face to the center of the convex hull.
 * @return The number of faces. If less than zero an error occured when trying to create the convex hull
 */
inline int createConvexHull(VectorVector3d& vertices,
                            std::vector<int>& faces,
                            const VectorVector3d& input,
                            double shrink = -1,
                            double shrinkClamp = -1)
{
  vertices.clear();
  faces.clear();

  btConvexHullComputer conv;
  btAlignedObjectArray<btVector3> points;
  points.reserve(static_cast<int>(input.size()));
  for (const auto& v : input)
  {
    points.push_back(btVector3(static_cast<btScalar>(v[0]), static_cast<btScalar>(v[1]), static_cast<btScalar>(v[2])));
  }

  btScalar val = conv.compute(&points[0].getX(),
                              sizeof(btVector3),
                              points.size(),
                              static_cast<btScalar>(shrink),
                              static_cast<btScalar>(shrinkClamp));
  if (val < 0)
  {
    ROS_ERROR("Failed to create convex hull");
    return -1;
  }

  int num_verts = conv.vertices.size();
  vertices.reserve(static_cast<size_t>(num_verts));
  for (int i = 0; i < num_verts; i++)
  {
    btVector3& v = conv.vertices[i];
    vertices.push_back(Eigen::Vector3d(v.getX(), v.getY(), v.getZ()));
  }

  int num_faces = conv.faces.size();
  faces.reserve(static_cast<size_t>(3 * num_faces));
  for (int i = 0; i < num_faces; i++)
  {
    std::vector<int> face;
    face.reserve(3);

    const btConvexHullComputer::Edge* sourceEdge = &(conv.edges[conv.faces[i]]);
    int a = sourceEdge->getSourceVertex();
    face.push_back(a);

    int b = sourceEdge->getTargetVertex();
    face.push_back(b);

    const btConvexHullComputer::Edge* edge = sourceEdge->getNextEdgeOfFace();
    int c = edge->getTargetVertex();
    face.push_back(c);

    edge = edge->getNextEdgeOfFace();
    c = edge->getTargetVertex();
    while (c != a)
    {
      face.push_back(c);

      edge = edge->getNextEdgeOfFace();
      c = edge->getTargetVertex();
    }
    faces.push_back(static_cast<int>(face.size()));
    faces.insert(faces.end(), face.begin(), face.end());
  }

  return num_faces;
}

/**
 * @brief Write a simple ply file given vertices and faces
 * @param path The file path
 * @param vertices A vector of vertices
 * @param faces The first values indicates the number of vertices that define the face followed by the vertice index
 * @param num_faces The number of faces
 * @return False if failed to write file, otherwise true
 */
inline bool writeSimplePlyFile(const std::string& path,
                               const VectorVector3d& vertices,
                               const std::vector<int>& faces,
                               int num_faces)
{
  //  ply
  //  format ascii 1.0           { ascii/binary, format version number }
  //  comment made by Greg Turk  { comments keyword specified, like all lines }
  //  comment this file is a cube
  //  element vertex 8           { define "vertex" element, 8 of them in file }
  //  property float x           { vertex contains float "x" coordinate }
  //  property float y           { y coordinate is also a vertex property }
  //  property float z           { z coordinate, too }
  //  element face 6             { there are 6 "face" elements in the file }
  //  property list uchar int vertex_index { "vertex_indices" is a list of ints }
  //  end_header                 { delimits the end of the header }
  //  0 0 0                      { start of vertex list }
  //  0 0 1
  //  0 1 1
  //  0 1 0
  //  1 0 0
  //  1 0 1
  //  1 1 1
  //  1 1 0
  //  4 0 1 2 3                  { start of face list }
  //  4 7 6 5 4
  //  4 0 4 5 1
  //  4 1 5 6 2
  //  4 2 6 7 3
  //  4 3 7 4 0
  std::ofstream myfile;
  myfile.open(path);
  if (myfile.fail())
  {
    ROS_ERROR("Failed to open file: %s", path.c_str());
    return false;
  }

  myfile << "ply\n";
  myfile << "format ascii 1.0\n";
  myfile << "comment made by tesseract\n";
  myfile << "element vertex " << vertices.size() << "\n";
  myfile << "property double x\n";
  myfile << "property double y\n";
  myfile << "property double z\n";
  myfile << "element face " << num_faces << "\n";
  myfile << "property list uchar uint vertex_indices\n";
  myfile << "end_header\n";

  // Add vertices
  for (const auto& v : vertices)
  {
    myfile << std::fixed << std::setprecision(std::numeric_limits<double>::digits10 + 1) << v[0] << " " << v[1] << " "
           << v[2] << "\n";
  }

  // Add faces
  size_t idx = 0;
  for (size_t i = 0; i < static_cast<size_t>(num_faces); ++i)
  {
    size_t num_vert = static_cast<size_t>(faces[idx]);
    for (size_t j = 0; j < num_vert; ++j)
    {
      myfile << faces[idx] << " ";
      ++idx;
    }
    myfile << faces[idx] << "\n";
    ++idx;
  }

  myfile.close();
  return true;
}
}

#endif  // TESSERACT_COLLISION_CONTACT_CHECKER_COMMON_H
