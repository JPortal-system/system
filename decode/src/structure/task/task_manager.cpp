#include <iostream>

#include "structure/task/task.hpp"
#include "structure/task/task_manager.hpp"


bool TaskManager::commitTask(Task* task){
    if(task==nullptr)return false;
    task->setTaskManager(this);

    std::unique_lock<std::mutex> guard(_mu);
    _undoTasks.emplace_back(task);
    return true;
}

bool TaskManager::commitTasks(std::list<Task*> &taskList){
    std::unique_lock<std::mutex> guard(_mu);
    for(auto task_ptr:taskList){
        if(task_ptr!=nullptr){
            task_ptr->setTaskManager(this);
            _undoTasks.emplace_back(task_ptr);
        }
    }
    return 0;
}

Task* TaskManager::getTask(){
    Task* task = nullptr;
    std::unique_lock<std::mutex> guard(_mu);
    if(!_undoTasks.empty()){
        task=_undoTasks.front();
        _undoTasks.pop_front();
    }

    return task;
}

bool TaskManager::isNeedMoreWorker() const{
    if(_undoTasks.empty()){
        return false;
    }
    return true;
}