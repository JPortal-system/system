#include <iostream>
#include <list>
#include <thread>

#include "structure/task/task.hpp"
#include "structure/task/task_manager.hpp"

std::mutex DemoTask::_mu;

Task* DemoTask::doTask(){
    std::unique_lock<std::mutex> guard(DemoTask::_mu);
    std::cout<<std::this_thread::get_id() <<"\tstart doing task"<<_i<<std::endl;
    std::chrono::milliseconds dura( 2000 );
    std::this_thread::sleep_for(dura);
    std::cout<<std::this_thread::get_id() <<"\tcompleted task"<<_i<<std::endl;
    return nullptr;
}

Task* CreateDemoTaskTask::doTask(){
    std::list<Task*> demos;
    for(int i=_start;i<(_start+_num);++i){
        DemoTask* t = new DemoTask(i);
        demos.push_back(t);
    }
    _taskManager->commitTasks(demos);
    return nullptr;
}