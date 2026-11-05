/****************************************************************************
** Meta object code from reading C++ file 'BotEngine.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/bot/BotEngine.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BotEngine.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN9BotEngineE_t {};
} // unnamed namespace

template <> constexpr inline auto BotEngine::qt_create_metaobjectdata<qt_meta_tag_ZN9BotEngineE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "BotEngine",
        "started",
        "",
        "stopped",
        "reason",
        "pausedChanged",
        "paused",
        "betPlaced",
        "BetRecord",
        "record",
        "logMessage",
        "message",
        "isError",
        "scriptError",
        "scriptOutput",
        "gameChanged",
        "Game",
        "game",
        "runGameChanged",
        "statsReset",
        "seedRotationRequested",
        "clientSeed",
        "vaultRequested",
        "amount",
        "manualGameUpdated",
        "QJsonObject",
        "bet",
        "active",
        "autoGameUpdated",
        "start",
        "stop",
        "pause",
        "resume",
        "placeNextBet",
        "onLiveFinished",
        "LiveBetResult",
        "result"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'started'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'stopped'
        QtMocHelpers::SignalData<void(const QString &)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Signal 'pausedChanged'
        QtMocHelpers::SignalData<void(bool)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Signal 'betPlaced'
        QtMocHelpers::SignalData<void(const BetRecord &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Signal 'logMessage'
        QtMocHelpers::SignalData<void(const QString &, bool)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 }, { QMetaType::Bool, 12 },
        }}),
        // Signal 'scriptError'
        QtMocHelpers::SignalData<void(const QString &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 },
        }}),
        // Signal 'scriptOutput'
        QtMocHelpers::SignalData<void(const QString &, bool)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 }, { QMetaType::Bool, 12 },
        }}),
        // Signal 'gameChanged'
        QtMocHelpers::SignalData<void(Game)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 16, 17 },
        }}),
        // Signal 'runGameChanged'
        QtMocHelpers::SignalData<void(Game)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 16, 17 },
        }}),
        // Signal 'statsReset'
        QtMocHelpers::SignalData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'seedRotationRequested'
        QtMocHelpers::SignalData<void(const QString &)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 21 },
        }}),
        // Signal 'vaultRequested'
        QtMocHelpers::SignalData<void(double)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 23 },
        }}),
        // Signal 'manualGameUpdated'
        QtMocHelpers::SignalData<void(Game, const QJsonObject &, bool)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 16, 17 }, { 0x80000000 | 25, 26 }, { QMetaType::Bool, 27 },
        }}),
        // Signal 'autoGameUpdated'
        QtMocHelpers::SignalData<void(Game, const QJsonObject &, bool)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 16, 17 }, { 0x80000000 | 25, 26 }, { QMetaType::Bool, 27 },
        }}),
        // Slot 'start'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void(const QString &)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Slot 'pause'
        QtMocHelpers::SlotData<void()>(31, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'resume'
        QtMocHelpers::SlotData<void()>(32, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'placeNextBet'
        QtMocHelpers::SlotData<void()>(33, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onLiveFinished'
        QtMocHelpers::SlotData<void(const LiveBetResult &)>(34, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 35, 36 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BotEngine, qt_meta_tag_ZN9BotEngineE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject BotEngine::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BotEngineE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BotEngineE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9BotEngineE_t>.metaTypes,
    nullptr
} };

void BotEngine::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BotEngine *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->started(); break;
        case 1: _t->stopped((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->pausedChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->betPlaced((*reinterpret_cast<std::add_pointer_t<BetRecord>>(_a[1]))); break;
        case 4: _t->logMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 5: _t->scriptError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->scriptOutput((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 7: _t->gameChanged((*reinterpret_cast<std::add_pointer_t<Game>>(_a[1]))); break;
        case 8: _t->runGameChanged((*reinterpret_cast<std::add_pointer_t<Game>>(_a[1]))); break;
        case 9: _t->statsReset(); break;
        case 10: _t->seedRotationRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->vaultRequested((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 12: _t->manualGameUpdated((*reinterpret_cast<std::add_pointer_t<Game>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3]))); break;
        case 13: _t->autoGameUpdated((*reinterpret_cast<std::add_pointer_t<Game>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3]))); break;
        case 14: _t->start(); break;
        case 15: _t->stop((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->stop(); break;
        case 17: _t->pause(); break;
        case 18: _t->resume(); break;
        case 19: _t->placeNextBet(); break;
        case 20: _t->onLiveFinished((*reinterpret_cast<std::add_pointer_t<LiveBetResult>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)()>(_a, &BotEngine::started, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(const QString & )>(_a, &BotEngine::stopped, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(bool )>(_a, &BotEngine::pausedChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(const BetRecord & )>(_a, &BotEngine::betPlaced, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(const QString & , bool )>(_a, &BotEngine::logMessage, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(const QString & )>(_a, &BotEngine::scriptError, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(const QString & , bool )>(_a, &BotEngine::scriptOutput, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(Game )>(_a, &BotEngine::gameChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(Game )>(_a, &BotEngine::runGameChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)()>(_a, &BotEngine::statsReset, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(const QString & )>(_a, &BotEngine::seedRotationRequested, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(double )>(_a, &BotEngine::vaultRequested, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(Game , const QJsonObject & , bool )>(_a, &BotEngine::manualGameUpdated, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (BotEngine::*)(Game , const QJsonObject & , bool )>(_a, &BotEngine::autoGameUpdated, 13))
            return;
    }
}

const QMetaObject *BotEngine::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BotEngine::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BotEngineE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int BotEngine::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 21)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 21;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 21)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 21;
    }
    return _id;
}

// SIGNAL 0
void BotEngine::started()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void BotEngine::stopped(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void BotEngine::pausedChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void BotEngine::betPlaced(const BetRecord & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void BotEngine::logMessage(const QString & _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void BotEngine::scriptError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void BotEngine::scriptOutput(const QString & _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2);
}

// SIGNAL 7
void BotEngine::gameChanged(Game _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void BotEngine::runGameChanged(Game _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void BotEngine::statsReset()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void BotEngine::seedRotationRequested(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void BotEngine::vaultRequested(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1);
}

// SIGNAL 12
void BotEngine::manualGameUpdated(Game _t1, const QJsonObject & _t2, bool _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1, _t2, _t3);
}

// SIGNAL 13
void BotEngine::autoGameUpdated(Game _t1, const QJsonObject & _t2, bool _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2, _t3);
}
QT_WARNING_POP
