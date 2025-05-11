#include "../include/threadpool.h"
#include <functional>
#include <thread>
#include <chrono>

const int TASK_MAX_THRESHHOLD = 4;

//////////////////////////////////////////////线程池方法/////////////////////////////////////////
//线程构造
ThreadPool::ThreadPool()
    : initThreadSize_(0)
    , taskSize_(0)
    , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
{}

//线程析构
ThreadPool::~ThreadPool()
{}

//设置线程模式
void ThreadPool::setPoolMode(PoolMode mode)
{
    poolMode_ = mode;
}

//设置任务数量上限
void ThreadPool::setTaskMaxQueThreshHold(int threshhold)
{
    taskQueMaxThreshHold_ = threshhold;
}

//用户提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    //获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    //线程通信，如果任务队列有空余则放入任务
    //如果等待一秒钟后任务队列还是不空，则返回提交错误信息
    if (!notFull_.wait_for(lock, std::chrono::seconds(1), 
        [&]()->bool { return taskQue_.size() < taskQueMaxThreshHold_; }))
    {
        std::cerr << "taskQue is full, submit fail!" << std::endl;
        return Result(sp, false);
    }

    //放入任务
    taskQue_.emplace(sp);
    taskSize_++;
    
    //通知任务队列有任务可以提取了
    notEmpty_.notify_all();
    return Result(sp);
}

//启动线程池
void ThreadPool::start(int initThreadSize)
{
    initThreadSize_ = initThreadSize;
    for (int i = 0; i < initThreadSize_; ++i)
    {
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
        threads_.emplace_back(std::move(ptr)); //unique_ptr没有普通的左值拷贝构造函数
    }
    for (int i = 0; i < initThreadSize_; ++i)
    {
        threads_[i]->start();
    }
}

//线程函数
void ThreadPool::threadFunc()
{

    for(;;)
    {
        std::shared_ptr<Task> task;
        //线程拿到任务就释放锁
        {
            //获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            //测试代码
            std::cout << "tid" << std::this_thread::get_id() << "尝试获取任务" << std::endl;
            //线程通信，等待任务队列有任务
            notEmpty_.wait(lock, [&]()->bool { return taskQue_.size() > 0; });

            std::cout << "tid" << std::this_thread::get_id() << "获取任务成功" << std::endl;
            //取出一个任务
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            //如果还有任务则通知其他线程取任务
            if (taskQue_.size() > 0)
            {
                notEmpty_.notify_all();
            }
        }

        //通知任务队列已经不满
        notFull_.notify_all();

        //当前线程执行任务
        if (task != nullptr)
        {
            task->exec();
        }
    }
}

//////////////////////////////////////////////线程方法/////////////////////////////////////////
Thread::Thread(ThreadFunc func)
    : func_(func)
{}

Thread::~Thread()
{}

//启动线程
void Thread::start()
{
    std::thread t(func_); //创建一个线程执行一个线程函数
    t.detach(); //线程函数在start函数执行完之后还需要存在，设置分离线程
}

//////////////////////////////////////////////Result方法/////////////////////////////////////////
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : task_(task)
    , isValid_(isValid)
{
    task_->setResult(this);
}

//Result res = pool.submitTask()
//int sum = res.get().cast()用户调用get方法时，如果任务还没执行，返回空

//setVal方法
void Result::setVal(Any any)
{
    any_ = std::move(any);
    sem_.post();
}

//get方法
Any Result::get()
{
    if (!isValid_)
    {
        return "";
    }
    sem_.wait();
    return std::move(any_);
}

//////////////////////////////////////////////Task方法/////////////////////////////////////////
Task::Task()
    : res_(nullptr)
{}

void Task::setResult(Result* res)
{
    res_ = std::move(res);
}

void Task::exec()
{
    res_->setVal(run());
}