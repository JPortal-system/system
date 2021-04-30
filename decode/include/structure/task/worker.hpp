#ifndef WORKER_HPP
#define WORKER_HPP

#include <iostream>
#include <atomic>

class TaskManager;

class Worker {
private:
    // true == alive
    // false == dead
    bool* _state_ptr;
protected:
    TaskManager* _taskManager;

public: 
    Worker(bool* state_ptr, TaskManager* taskManager){
        _state_ptr = state_ptr;
        *_state_ptr = true;
        _taskManager = taskManager;
    }
    // int getId() const { return _id; }
    void operator()(){
        int res = work();
        *_state_ptr = false;
    };
    int work();
};

// class MatchWorker: public Worker {
// private:

// public:
//     MatchWorker(int id, TaskManager* taskManager):Worker(id, taskManager){}
    
//     void operator()(){
//         work();
//     };

//     int work();
// };



#endif // WORKER_HPP