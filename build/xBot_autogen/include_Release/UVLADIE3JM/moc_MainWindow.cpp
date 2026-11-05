/****************************************************************************
** Meta object code from reading C++ file 'MainWindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/MainWindow.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MainWindow.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto MainWindow::qt_create_metaobjectdata<qt_meta_tag_ZN10MainWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MainWindow",
        "onRollOnce",
        "",
        "onStartAuto",
        "onStopAuto",
        "onPauseAuto",
        "onResetSession",
        "onClearHistory",
        "onRotateSeed",
        "rotateSeed",
        "fromScript",
        "clientSeed",
        "applyClientSeed",
        "std::function<void()>",
        "then",
        "onEngineStarted",
        "onEngineStopped",
        "reason",
        "onPausedChanged",
        "paused",
        "onBetPlaced",
        "BetRecord",
        "record",
        "onBetTableClicked",
        "QModelIndex",
        "index",
        "onBetTableContextMenu",
        "QPoint",
        "pos",
        "onLogMessage",
        "message",
        "isError",
        "onSearchEdited",
        "text",
        "onSwitchToChart",
        "onTabChanged",
        "onToggleDarkTheme",
        "enabled",
        "onOpenOptions",
        "onOpenFile",
        "onOpenConditionBuilder",
        "onRunScript",
        "script",
        "ScriptLanguage",
        "language",
        "onAbout",
        "onGameSelected",
        "Game",
        "game",
        "onCasinoSelected",
        "LiveSettings::Casino",
        "casino",
        "onBalancesReady",
        "QMap<QString,double>",
        "balances",
        "onBalancesFailed",
        "error",
        "onExtensionReadyChanged",
        "ready",
        "onBalanceTimer"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onRollOnce'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStartAuto'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStopAuto'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPauseAuto'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onResetSession'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onClearHistory'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRotateSeed'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'rotateSeed'
        QtMocHelpers::SlotData<void(bool, const QString &)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 10 }, { QMetaType::QString, 11 },
        }}),
        // Slot 'rotateSeed'
        QtMocHelpers::SlotData<void(bool)>(9, 2, QMC::AccessPrivate | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Slot 'applyClientSeed'
        QtMocHelpers::SlotData<void(const std::function<void()> &)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Slot 'onEngineStarted'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onEngineStopped'
        QtMocHelpers::SlotData<void(const QString &)>(16, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'onPausedChanged'
        QtMocHelpers::SlotData<void(bool)>(18, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 19 },
        }}),
        // Slot 'onBetPlaced'
        QtMocHelpers::SlotData<void(const BetRecord &)>(20, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 21, 22 },
        }}),
        // Slot 'onBetTableClicked'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(23, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 24, 25 },
        }}),
        // Slot 'onBetTableContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(26, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 27, 28 },
        }}),
        // Slot 'onLogMessage'
        QtMocHelpers::SlotData<void(const QString &, bool)>(29, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 30 }, { QMetaType::Bool, 31 },
        }}),
        // Slot 'onSearchEdited'
        QtMocHelpers::SlotData<void(const QString &)>(32, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 33 },
        }}),
        // Slot 'onSwitchToChart'
        QtMocHelpers::SlotData<void()>(34, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTabChanged'
        QtMocHelpers::SlotData<void(int)>(35, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 25 },
        }}),
        // Slot 'onToggleDarkTheme'
        QtMocHelpers::SlotData<void(bool)>(36, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 37 },
        }}),
        // Slot 'onOpenOptions'
        QtMocHelpers::SlotData<void()>(38, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onOpenFile'
        QtMocHelpers::SlotData<void()>(39, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onOpenConditionBuilder'
        QtMocHelpers::SlotData<void()>(40, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRunScript'
        QtMocHelpers::SlotData<void(const QString &, ScriptLanguage)>(41, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 42 }, { 0x80000000 | 43, 44 },
        }}),
        // Slot 'onAbout'
        QtMocHelpers::SlotData<void()>(45, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onGameSelected'
        QtMocHelpers::SlotData<void(Game)>(46, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 47, 48 },
        }}),
        // Slot 'onCasinoSelected'
        QtMocHelpers::SlotData<void(LiveSettings::Casino)>(49, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 50, 51 },
        }}),
        // Slot 'onBalancesReady'
        QtMocHelpers::SlotData<void(const QMap<QString,double> &)>(52, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 53, 54 },
        }}),
        // Slot 'onBalancesFailed'
        QtMocHelpers::SlotData<void(const QString &)>(55, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 56 },
        }}),
        // Slot 'onExtensionReadyChanged'
        QtMocHelpers::SlotData<void(bool)>(57, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 58 },
        }}),
        // Slot 'onBalanceTimer'
        QtMocHelpers::SlotData<void()>(59, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MainWindow, qt_meta_tag_ZN10MainWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10MainWindowE_t>.metaTypes,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onRollOnce(); break;
        case 1: _t->onStartAuto(); break;
        case 2: _t->onStopAuto(); break;
        case 3: _t->onPauseAuto(); break;
        case 4: _t->onResetSession(); break;
        case 5: _t->onClearHistory(); break;
        case 6: _t->onRotateSeed(); break;
        case 7: _t->rotateSeed((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 8: _t->rotateSeed((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->applyClientSeed((*reinterpret_cast<std::add_pointer_t<std::function<void()>>>(_a[1]))); break;
        case 10: _t->onEngineStarted(); break;
        case 11: _t->onEngineStopped((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->onPausedChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 13: _t->onBetPlaced((*reinterpret_cast<std::add_pointer_t<BetRecord>>(_a[1]))); break;
        case 14: _t->onBetTableClicked((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 15: _t->onBetTableContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 16: _t->onLogMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 17: _t->onSearchEdited((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 18: _t->onSwitchToChart(); break;
        case 19: _t->onTabChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 20: _t->onToggleDarkTheme((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 21: _t->onOpenOptions(); break;
        case 22: _t->onOpenFile(); break;
        case 23: _t->onOpenConditionBuilder(); break;
        case 24: _t->onRunScript((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<ScriptLanguage>>(_a[2]))); break;
        case 25: _t->onAbout(); break;
        case 26: _t->onGameSelected((*reinterpret_cast<std::add_pointer_t<Game>>(_a[1]))); break;
        case 27: _t->onCasinoSelected((*reinterpret_cast<std::add_pointer_t<LiveSettings::Casino>>(_a[1]))); break;
        case 28: _t->onBalancesReady((*reinterpret_cast<std::add_pointer_t<QMap<QString,double>>>(_a[1]))); break;
        case 29: _t->onBalancesFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 30: _t->onExtensionReadyChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 31: _t->onBalanceTimer(); break;
        default: ;
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 32)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 32;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 32)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 32;
    }
    return _id;
}
QT_WARNING_POP
