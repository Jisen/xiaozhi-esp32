#ifndef GOODBYE_EVENT_H
#define GOODBYE_EVENT_H

#include <functional>
#include <vector>

class GoodbyeEventManager {
public:
    static GoodbyeEventManager& GetInstance() {
        static GoodbyeEventManager instance;
        return instance;
    }
    
    // 注册goodbye事件回调
    void RegisterGoodbyeCallback(std::function<void()> callback) {
        callbacks_.push_back(callback);
    }
    
    // 触发goodbye事件
    void NotifyGoodbye() {
        for (auto& callback : callbacks_) {
            if (callback) {
                callback();
            }
        }
    }
    
private:
    std::vector<std::function<void()>> callbacks_;
};

#endif // GOODBYE_EVENT_H 