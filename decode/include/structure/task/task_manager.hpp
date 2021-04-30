#ifndef TASK_MANAGER_HPP
#define TASK_MANAGER_HPP
#include <mutex>
#include <list>
#include <set>
#include <memory>

class Task;

class TaskManager {
public:
private:
    std::mutex _mu;
    // std::unique_ptr<std::mutex> m;
    std::list<Task*> _undoTasks;
    // std::set<Task*> doneTasks;
public:
    bool commitTask(Task* task);
    bool commitTasks(std::list<Task*> &taskList);
    Task* getTask();
    bool isNeedMoreWorker() const;
    int getTaskSize() const{
        return _undoTasks.size();
    }
};

// class MatchTaskManager:public TaskManager {
// private:
//     std::mutex m;

// public:
//     MatchTaskManager(){};
//     int commitTasks();
//     Task* getTask();
//     bool isCompleted();
// };

#endif // TASK_MANAGER_HPP