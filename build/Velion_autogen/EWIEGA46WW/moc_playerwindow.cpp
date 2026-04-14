/****************************************************************************
** Meta object code from reading C++ file 'playerwindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../playerwindow.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'playerwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.0. It"
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
struct qt_meta_tag_ZN12PlayerWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto PlayerWindow::qt_create_metaobjectdata<qt_meta_tag_ZN12PlayerWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "PlayerWindow",
        "mprisPlaybackStateChanged",
        "",
        "mprisMetadataChanged",
        "mprisPositionChanged",
        "mprisVolumeChanged",
        "mprisPlay",
        "mprisPause",
        "mprisPlayPause",
        "mprisStop",
        "mprisNext",
        "mprisPrevious",
        "mprisSetPosition",
        "position",
        "mprisSetVolume",
        "volume",
        "mprisSetLoopStatus",
        "loopStatus",
        "mprisSetShuffle",
        "enabled",
        "mprisRaise"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'mprisPlaybackStateChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'mprisMetadataChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'mprisPositionChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'mprisVolumeChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'mprisPlay'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'mprisPause'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'mprisPlayPause'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'mprisStop'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'mprisNext'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'mprisPrevious'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'mprisSetPosition'
        QtMocHelpers::SlotData<void(qint64)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 13 },
        }}),
        // Slot 'mprisSetVolume'
        QtMocHelpers::SlotData<void(double)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 15 },
        }}),
        // Slot 'mprisSetLoopStatus'
        QtMocHelpers::SlotData<void(const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'mprisSetShuffle'
        QtMocHelpers::SlotData<void(bool)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 19 },
        }}),
        // Slot 'mprisRaise'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<PlayerWindow, qt_meta_tag_ZN12PlayerWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject PlayerWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12PlayerWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12PlayerWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12PlayerWindowE_t>.metaTypes,
    nullptr
} };

void PlayerWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PlayerWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->mprisPlaybackStateChanged(); break;
        case 1: _t->mprisMetadataChanged(); break;
        case 2: _t->mprisPositionChanged(); break;
        case 3: _t->mprisVolumeChanged(); break;
        case 4: _t->mprisPlay(); break;
        case 5: _t->mprisPause(); break;
        case 6: _t->mprisPlayPause(); break;
        case 7: _t->mprisStop(); break;
        case 8: _t->mprisNext(); break;
        case 9: _t->mprisPrevious(); break;
        case 10: _t->mprisSetPosition((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 11: _t->mprisSetVolume((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 12: _t->mprisSetLoopStatus((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->mprisSetShuffle((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 14: _t->mprisRaise(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PlayerWindow::*)()>(_a, &PlayerWindow::mprisPlaybackStateChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlayerWindow::*)()>(_a, &PlayerWindow::mprisMetadataChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlayerWindow::*)()>(_a, &PlayerWindow::mprisPositionChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlayerWindow::*)()>(_a, &PlayerWindow::mprisVolumeChanged, 3))
            return;
    }
}

const QMetaObject *PlayerWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PlayerWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12PlayerWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int PlayerWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 15)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 15;
    }
    return _id;
}

// SIGNAL 0
void PlayerWindow::mprisPlaybackStateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void PlayerWindow::mprisMetadataChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void PlayerWindow::mprisPositionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void PlayerWindow::mprisVolumeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
QT_WARNING_POP
