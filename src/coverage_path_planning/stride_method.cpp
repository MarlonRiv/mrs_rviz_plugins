#include "coverage_path_planning/stride_method.h"
#include "coverage_path_planning/planner_tool.h"

#include <ros/ros.h>

#include <rviz/ogre_helpers/line.h>

#include <vector>
#include <cmath>
#include <limits>
#include <queue>

namespace mrs_rviz_plugins{

void StrideMethod::initialize (rviz::Property* property_container, Ogre::SceneManager* scene_manager, Ogre::SceneNode* root_node){
  ApproximateDecomposition::initialize(property_container, scene_manager, root_node);

  turn_num_property_ = new rviz::IntProperty("Turns", 0, "Number of turns in current path", property_container);
  drone_name_property_ = new rviz::EditableEnumProperty("Uav", "", "Uav used to perform coverage mission", property_container);

  cell_num_property_->setReadOnly(true);
  turn_num_property_->setReadOnly(true);

  std::vector<std::string> drone_names = PlannerTool::getUavNames();
  for(auto& name : drone_names){
    drone_name_property_->addOption(name.c_str());
  }

  if(drone_names.size() > 0){
    drone_name_property_->setString(drone_names[0].c_str());
  }else{
    ROS_WARN("[StrideMethod]: could not find any uav for coverage mission");
  }
}

void StrideMethod::compute(){
  float min_dist = std::numeric_limits<float>::max();
  Ogre::Vector2 index_min;
  
  std::cout << "compute entered\n";
  std::cout << "start: " << start_position_.x << " " << start_position_.y << std::endl;
  // Searching for the closest cell to the start position.
  for(size_t i=0; i<grid.size(); ++i){
    for(size_t j=0; j<grid[i].size(); ++j){
      float cur_dist;
      if(!grid[i][j].valid){
        continue;
      }

      cur_dist = std::pow(grid[i][j].x - start_position_.x, 2) + std::pow(grid[i][j].y - start_position_.y, 2);
      if(cur_dist < min_dist){
        index_min.x = i;
        index_min.y = j;
        min_dist = cur_dist;
      }
    }
  }

  std::cout << "closest cell found\n";
  start_position_.x = grid[index_min.x][index_min.y].x;
  start_position_.y = grid[index_min.x][index_min.y].y;

  // |------------------- Algorithm -------------------|
  
  // 1. Set the current cell to the initial cell.
  Ogre::Vector2 cur_cell;
  cur_cell = index_min;
  path_.request.path.points.clear();
  path_.request.path.header.frame_id   = polygon_frame_;
  path_.request.path.fly_now           = true;
  path_.request.path.stop_at_waypoints = true;
  path_.request.path.loop              = false;
  addCellToPath(cur_cell);
  std::cout << "1. processed\n";

  while(true){
    // 2. Find all unvisited neighbor cells of the current cell
    std::vector<Ogre::Vector2> directions {
      Ogre::Vector2(0, 1),
      Ogre::Vector2(0, -1),
      Ogre::Vector2(1, 0),
      Ogre::Vector2(-1, 0)
    };
    std::vector<size_t> valid_directions;
    for(size_t i=0; i<directions.size(); ++i){
      if(!isLimit(cur_cell + directions[i])){
        valid_directions.push_back(i);
      }
    }

    std::cout << "2. processed\n";

    // 2.1 If no neighbor has been found, find 
    // the nearest unvisited cell located next to cells already visited.
    if(valid_directions.size() == 0){
      std::cout << "getPathToNextCell\n";
      std::vector<Ogre::Vector2> path_to_next = getPathToNextCell(cur_cell);
      for(auto& cell : path_to_next){
        std::cout << cell.x << " " << cell.y << std::endl;
        addCellToPath(cell);
      }

      if(path_to_next.size() == 0){
        ROS_WARN("[StrideMethod]: Could not find the path to unvisited cell. Terminating algorithm");
        break;
      }
      
      std::cout << "size: "<< path_to_next.size() << std::endl; 
      cur_cell = path_to_next.back();
      continue;
    }

    // 3. Generate the longest possible stride in the direction
    // of each unvisited neighbor cell.
    std::vector<stride_t> strides;
    for(size_t& index : valid_directions){
      strides.push_back(computeStride(cur_cell, directions[index]));
    }

    std::cout << "3. processed\n";
    // 4. Select the longest stride.
    stride_t longest_stride;
    longest_stride.len = 0;
    for(stride_t& stride : strides){
      if(stride.len > longest_stride.len){
        longest_stride = stride;
      }
    }

    std::cout << "4. processed\n";
    std::cout << "longest stride:\n " 
              << longest_stride.start.x << std::endl
              << longest_stride.start.y << std::endl
              << longest_stride.direction.x << std::endl
              << longest_stride.direction.y << std::endl
              << longest_stride.len << std::endl;

    // 5. Add all cells of the stride to the path and mark them
    // as visited.
    Ogre::Vector2 last_cell = longest_stride.start;
    for(size_t i=0; i<longest_stride.len; i++){
      grid[last_cell.x][last_cell.y].visited = true;
      last_cell = last_cell + longest_stride.direction;
    }
    std::cout << "last_cell: " << last_cell.x << " " << last_cell.y << std::endl;
    addCellToPath(last_cell);

    std::cout << "5. processed\n";

    // 6. Set the current cell to the last cell of the stride.
    cur_cell = last_cell;

    std::cout << "6. processed\n";

    // 7. Repeat starting at point 2 until all cells have been
    // visited.
    bool finished = true;
    for(auto& vector : grid){
      for(auto& cell : vector){
        if(!cell.visited && cell.valid){
          finished = false;
          std::cout << "cell " << cell.x << " " << cell.y << " has not been visited yet\n";
        }
      }
    }
    std::cout << "7. processed\n";
    if(finished){
      break;
    }
  }
  is_computed_ = true;
  
  // Cleaning the grid for future computations.
  for(auto& vector : grid)
    for(auto& cell : vector)
      cell.visited = false;

  // Draw the path
  if(path_node_){
    scene_manager_->destroySceneNode(path_node_);
  }
  path_node_ = root_node_->createChildSceneNode();

  for(size_t i=0; i<path_.request.path.points.size()-1; ++i){
    geometry_msgs::Point start = path_.request.path.points[i].position;
    geometry_msgs::Point end = path_.request.path.points[i+1].position;
    rviz::Line* line = new rviz::Line(scene_manager_, path_node_);
    line->setColor(1, 0, 0, 1);
    line->setPoints(Ogre::Vector3(start.x, start.y, start.z), Ogre::Vector3(end.x, end.y, end.z));
    line->setPosition(Ogre::Vector3(0, 0, 0));
    line->setScale(Ogre::Vector3(1, 1, 1));
    line->setVisible(true);
  }

  turn_num_property_->setInt(path_.request.path.points.size() - 1);
}

void StrideMethod::addCellToPath(Ogre::Vector2 cell){
  grid[cell.x][cell.y].visited = true;
  mrs_msgs::Reference ref;
  ref.position.x = grid[cell.x][cell.y].x;
  ref.position.y = grid[cell.x][cell.y].y;
  ref.position.z = height_;
  path_.request.path.points.push_back(ref);
}

// The function has been generated by ChatGPT
bool isValid(int row, int col, int numRows, int numCols) {
    return (row >= 0) && (row < numRows) && (col >= 0) && (col < numCols);
}

// The function has been generated by ChatGPT
std::vector<Ogre::Vector2> StrideMethod::getPathToNextCell(Ogre::Vector2 start){
  // Define possible movements: up, down, left, right, diagonally
  int dx[] = {-1, 1, 0, 0, 1, 1, -1, -1};
  int dy[] = {0, 0, -1, 1, 1, -1, -1, 1};

  int numRows = grid.size();
  int numCols = grid[0].size();

  // Create a queue for BFS
  std::queue<std::pair<Ogre::Vector2, std::vector<Ogre::Vector2>>> q;

  // Mark the start cell as visited and enqueue it with an empty path
  std::vector<std::vector<bool>> visited(numRows, std::vector<bool>(numCols, false));
  visited[start.x][start.y] = true;
  q.push({start, {}});

  // Perform BFS
  while (!q.empty()) {
    Ogre::Vector2 currCell = q.front().first;
    std::vector<Ogre::Vector2> path = q.front().second;
    q.pop();

    // Check if the current cell is the goal cell
    if (isNextToVisited(currCell)) {
      // Include the goal cell in the path and return it
      path.push_back(currCell);
      return path;
    }

    // Explore adjacent cells
    for (int i = 0; i < 8; i++) {
      int newRow = currCell.x + dx[i];
      int newCol = currCell.y + dy[i];

      // Check if the new cell is valid and not visited
      if (isValid(newRow, newCol, numRows, numCols) && !visited[newRow][newCol] && grid[newRow][newCol].valid) {
        // Mark the new cell as visited
        visited[newRow][newCol] = true;
        // Enqueue the new cell with the current path plus the new cell
        std::vector<Ogre::Vector2> newPath = path;
        newPath.push_back(currCell);
        q.push({{newRow, newCol}, newPath}); 
      }
    }
  }

  // If goal cell is not reachable, return an empty path
  return {};
}

bool StrideMethod::isNextToVisited(Ogre::Vector2 cell){
  if(grid[cell.x][cell.y].visited){
    return false;
  }

  bool result = false;
  std::vector<Ogre::Vector2> directions {
    Ogre::Vector2(0, 1),
    Ogre::Vector2(0, -1),
    Ogre::Vector2(1, 0),
    Ogre::Vector2(-1, 0)
  };

  for(size_t i=0; i<directions.size(); ++i){
    Ogre::Vector2 cur_cell = cell + directions[i];
    if(cur_cell.x < 0 || cur_cell.x >= grid.size()) {
      continue;
    }
    if(cur_cell.y < 0 || cur_cell.y >= grid[cell.x].size()){
      continue;
    }
    if(grid[cur_cell.x][cur_cell.y].visited && grid[cur_cell.x][cur_cell.y].valid){
      result = true;
    }
  }
  return result;
}

void StrideMethod::start(){
  if(!is_computed_){
    ROS_WARN("[StrideMethod]: Could not start the mission. The path has not been computed yet.");
    return;
  }
  ROS_INFO("[StrideMethod]: start() is called");
}

StrideMethod::stride_t StrideMethod::computeStride(Ogre::Vector2 start, Ogre::Vector2 direction){
  StrideMethod::stride_t result;
  result.start = start;
  result.direction = direction;
  result.len = 1;

  Ogre::Vector2 last_cell = start;
  Ogre::Vector2 next_cell;
  next_cell.x = start.x + direction.x;
  next_cell.y = start.y + direction.y;

  limit_t limits_start = getLimits(start, direction);

  while(true){
    last_cell = next_cell;
    next_cell.x = next_cell.x + direction.x; 
    next_cell.y = next_cell.y + direction.y;

    // std::cout << "next_cell: " << next_cell.x << " " << next_cell.y << std::endl;

    // We do not add next_cell if it is already in the generated path or 
    // if it falls outside the boundaries of the area
    if(next_cell.x < 0 || next_cell.x >= grid.size()) {
      break;
    }
    if(next_cell.y < 0 || next_cell.y >= grid[next_cell.x].size()){
      break;
    }
    if(grid[next_cell.x][next_cell.y].visited || !grid[next_cell.x][next_cell.y].valid){
      break;
    }

    limit_t limits_last = getLimits(last_cell, direction);
    limit_t limits_next = getLimits(next_cell, direction);

    // std::cout << "limits_last: " << limits_last.first << " " << limits_last.second << " " << limits_last.num << std::endl;
    // std::cout << "limits_next: " << limits_next.first << " " << limits_next.second << " " << limits_next.num << std::endl;
    // std::cout << "limits_start: " << limits_start.first << " " << limits_start.second << " " << limits_start.num << std::endl;

    // If getLimitNum(last_cell) = 2 we always add next_cell, because next_cell
    // is the only possible cell where we can go to from last_cell
    if(limits_last.num == 2){
      result.len++;
      continue;
    }

    // If getLimitNum(last_cell) != 2, any of the following conditions will 
    // prevent addition of next_cell to the stride:

    if(limits_next.num == 0){
      break;
    }

    if(limits_next.num != limits_start.num){
      break;
    }

    if(limits_next.num == 1 &&
      limits_next.first == limits_start.second && limits_next.second == limits_start.first){
      break;
    }

    result.len++;
  }

  return result;
}

StrideMethod::limit_t StrideMethod::getLimits(Ogre::Vector2 cell, Ogre::Vector2 direction){
  limit_t res;

  Ogre::Vector2 sample;
  sample.x = cell.x + direction.y;
  sample.y = cell.y + direction.x;
  if(isLimit(sample)){
    res.first = true;
    res.num++;
  }

  sample.x = cell.x - direction.y;
  sample.y = cell.y - direction.x;
  if(isLimit(sample)){
    res.second = true;
    res.num++;
  }

  return res;
}

bool StrideMethod::isLimit(Ogre::Vector2 cell){
  if(cell.x < 0 || cell.x >= grid.size()) {
    return true;
  }
  if(cell.y < 0 || cell.y >= grid[cell.x].size()){
    return true;
  }
  if(grid[cell.x][cell.y].visited || !grid[cell.x][cell.y].valid){
    return true;
  }
  return false;
}

} // namespace mrs_rviz_plugins

#include <pluginlib/class_list_macros.hpp>
PLUGINLIB_EXPORT_CLASS(mrs_rviz_plugins::StrideMethod, mrs_rviz_plugins::CoverageMethod)