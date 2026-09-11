#pragma once
#include "qt_compat.h"
#include <QObject>
#include <memory>

namespace ha {
// GUI-thread-only non-owning QObject guard. QPointer's inline destructor can
// free a control block allocated inside Valve's Qt through our different CRT.
// Keep our guard state in this module and observe destruction through signals.
template<class T> class UiPointer {
    struct State {
        T* object;
        QMetaObject::Connection connection;
        explicit State(T* value):object(value) {}
        ~State() { QObject::disconnect(connection); }
    };
    std::shared_ptr<State> state_;
public:
    UiPointer() = default;
    UiPointer(T* object) {
        if(!object)return;
        state_=std::make_shared<State>(object);
        std::weak_ptr<State> weak=state_;
        // The context-free overload delivers directly, including during teardown.
        state_->connection=QObject::connect(object,&QObject::destroyed,[weak] {
            if(auto state=weak.lock())state->object=nullptr;
        });
    }
    T* data() const noexcept {return state_ ? state_->object : nullptr;}
    T* operator->() const noexcept {return data();}
    operator T*() const noexcept {return data();}
};
}
