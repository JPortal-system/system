#include <iostream>
#include <thread>
#include "structure/task/worker.hpp"
#include "structure/task/task.hpp"
#include "structure/task/task_manager.hpp"

int Worker::work(){
    Task* task = _taskManager->getTask();
    while(task!=nullptr){
        task = task->doTask();
        if(task==nullptr){
            task = _taskManager->getTask();
        }
    }
    return 0;
}