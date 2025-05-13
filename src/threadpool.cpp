#include "../include/threadpool.h"
#include <functional>
#include <thread>
#include <chrono>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 200;
const int MAX_THREAD_IDLETIME = 60;
//////////////////////////////////////////////线程池方法/////////////////////////////////////////
//线程构造
ThreadPool::ThreadPool()
    : initThreadSize_(0)
    , maxThreadSize_(THREAD_MAX_THRESHHOLD)
    , curThreadSize_(0)
    , idleThreadSize_(0)
    , taskSize_(0)
    , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
    , isPoolRunning_(false)
{}

//线程析构
ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;

    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();
    exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}

//设置线程模式
void ThreadPool::setPoolMode(PoolMode mode)
{
    if (checkRunning())
        return;
    poolMode_ = mode;
}
 
//设置任务数量上限
void ThreadPool::setTaskMaxQueThreshHold(int threshhold)
{
    if (checkRunning())
        return;
    taskQueMaxThreshHold_ = threshhold;
}

//设置cached 模式下线程数量上限
void ThreadPool::setMaxThreadSize(int threshhold)
{
    if (checkRunning())
        return;
    if (poolMode_ == PoolMode::MODE_CACHED)
    {
        maxThreadSize_ = threshhold;
    }
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

    //cavhed模式下检查是否需要增加线程数量
    if (poolMode_ == PoolMode::MODE_CACHED
        && taskSize_ > idleThreadSize_
        && curThreadSize_ < maxThreadSize_)
    {
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        // threads_.emplace_back(std::move(ptr));
        int threadid = ptr->getId();
        threads_.emplace(threadid, std::move(ptr));
        threads_[threadid]->start();
        curThreadSize_++;
        idleThreadSize_++;

        std::cout << "create new thread" << std::endl;
    }
    

    return Result(sp);
}

//启动线程池
void ThreadPool::start(int initThreadSize)
{
    isPoolRunning_ = true;

    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    for (int i = 0; i < initThreadSize_; ++i)
    {
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        //threads_.emplace_back(std::move(ptr)); //unique_ptr没有普通的左值拷贝构造函数
        threads_.emplace(ptr->getId(), std::move(ptr));
    }
    for (int i = 0; i < initThreadSize_; ++i)
    {
        threads_[i]->start();
        idleThreadSize_++;
    }
}

//线程函数
void ThreadPool::threadFunc(int threadid)
{

    while(isPoolRunning_)
    {
        std::shared_ptr<Task> task;

        auto lastTime = std::chrono::high_resolution_clock().now();
        //idleThreadSize_++;
        //线程拿到任务就释放锁
        {
            //获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            
            //在cached模式下，超出initThreadSize部分的线程，如果空闲时间超过60秒就会被回收
            while (taskQue_.size() == 0)
            {
                if (poolMode_ == PoolMode::MODE_CACHED)
                {
                    //wait_for有两个返回值,no_timeout和timeout,分别表示有任务返回和超时返回
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= MAX_THREAD_IDLETIME
                            && curThreadSize_ > initThreadSize_)
                        {
                            //通过threadId回收多余线程
                            threads_.erase(threadid);
                            curThreadSize_--;
                            idleThreadSize_--;

                            std::cout << "tid" << std::this_thread::get_id() << "exit" << std::endl;

                            return;
                        }
                    }
                }
                else
                {
                    //线程通信，等待任务队列有任务
                    notEmpty_.wait(lock);
                }

                //正在等待中的线程被线程池析构函数唤醒后实现资源回收
                if (!isPoolRunning_)
                {
                    threads_.erase(threadid);
                    std::cout << "tid" << std::this_thread::get_id() << "exit" << std::endl;
                    exitCond_.notify_all();
                    return;
                }
            }

            idleThreadSize_--;

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
        idleThreadSize_++;

        lastTime = std::chrono::high_resolution_clock().now();
    }

    //正在执行任务的线程发现isPoolRunning = false之后实现资源回收
    threads_.erase(threadid);
    std::cout << "tid" << std::this_thread::get_id() << "exit" << std::endl;
    exitCond_.notify_all();
}

//检查线程池运行状态
bool ThreadPool::checkRunning() const
{
    return isPoolRunning_;
}

//////////////////////////////////////////////线程方法/////////////////////////////////////////
Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(genThreadId_++)
{}

Thread::~Thread()
{}

//获取threadId
int Thread::getId()
{
    return threadId_;
}

//启动线程
void Thread::start()
{
    std::thread t(func_, threadId_); //创建一个线程执行一个线程函数
    t.detach(); //线程函数在start函数执行完之后还需要存在，设置分离线程
}

int Thread::genThreadId_ = 0;

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