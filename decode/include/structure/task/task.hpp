#ifndef TASK_HPP
#define TASK_HPP

#include <map>
#include <string>
#include <mutex>

class TaskManager;

class Task{
public:
    enum class TaskKind{
        DEMOTASK = -1,
        DECODETASK = 0,
        MATCHTASK = 1,
        TASKKIND_COUNT
    };
    friend class TaskManager;
    friend class Worker;
    // friend class DemoTask;
    // friend class DecodeWorker;
    // friend class MatchWorker;
private:
    TaskKind _kind;
public:
    Task(TaskKind kind): _kind(kind){}
protected:
    TaskManager* _taskManager=nullptr;
    
    void setTaskManager(TaskManager* taskManager){
        _taskManager = taskManager;
    }
    virtual Task* doTask()=0;
};

class DemoTask: public Task{
private:
    static std::mutex _mu;
    int _i;
public:
    DemoTask(int i): Task(TaskKind::DEMOTASK){
        _i = i;
    }
protected:
    Task* doTask();
};

class CreateDemoTaskTask: public Task{
private:
    int _num;
    int _start;
public:
    CreateDemoTaskTask(int num, int start): Task(TaskKind::DEMOTASK){
        _num = num;
        _start = start;
    }
protected:
    Task* doTask();
};

#endif // TASK_HPP