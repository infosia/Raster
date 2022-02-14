#pragma once

#include <list>

namespace renderer
{

    enum class SubjectType {
        Error,
        Warning,
        Info,
        Progress
    };

    class IObserver
    {
    public:
        virtual ~IObserver() {}

        virtual void message(SubjectType, std::string) = 0;
        virtual void progress(float progress) = 0;
    };

    class Observable
    {
    public:
        static void subscribe(IObserver *observer)
        {
            observers.push_back(observer);
        }

        static void unsubscribe(IObserver *observer)
        {
            observers.remove(observer);
        }

        static void notifyMessage(SubjectType subjectType, std::string msg)
        {
            for (auto observer : observers) {
                observer->message(subjectType, msg);
            }
        }

        static void notifyProgress(float progress)
        {
            for (auto observer : observers) {
                observer->progress(progress);
            }
        }

    private:
        static std::list<IObserver *> observers;
    };
}