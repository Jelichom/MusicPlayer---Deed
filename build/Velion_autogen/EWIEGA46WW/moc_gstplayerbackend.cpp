/****************************************************************************
** Meta object code from reading C++ file 'gstplayerbackend.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../gstplayerbackend.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'gstplayerbackend.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN16GstPlayerBackendE_t {};
} // unnamed namespace

template <> constexpr inline auto GstPlayerBackend::qt_create_metaobjectdata<qt_meta_tag_ZN16GstPlayerBackendE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "GstPlayerBackend",
        "durationChanged",
        "",
        "duration",
        "positionChanged",
        "position",
        "playbackStateChanged",
        "GstPlayerBackend::PlaybackState",
        "state",
        "mediaStatusChanged",
        "GstPlayerBackend::MediaStatus",
        "status",
        "metaDataChanged",
        "volumeChanged",
        "volume",
        "PlaybackState",
        "StoppedState",
        "PlayingState",
        "PausedState",
        "MediaStatus",
        "NoMedia",
        "LoadedMedia",
        "BufferedMedia",
        "EndOfMedia",
        "InvalidMedia"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'durationChanged'
        QtMocHelpers::SignalData<void(qint64)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 3 },
        }}),
        // Signal 'positionChanged'
        QtMocHelpers::SignalData<void(qint64)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 5 },
        }}),
        // Signal 'playbackStateChanged'
        QtMocHelpers::SignalData<void(GstPlayerBackend::PlaybackState)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 8 },
        }}),
        // Signal 'mediaStatusChanged'
        QtMocHelpers::SignalData<void(GstPlayerBackend::MediaStatus)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 10, 11 },
        }}),
        // Signal 'metaDataChanged'
        QtMocHelpers::SignalData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'volumeChanged'
        QtMocHelpers::SignalData<void(double)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 14 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'PlaybackState'
        QtMocHelpers::EnumData<enum PlaybackState>(15, 15, QMC::EnumFlags{}).add({
            {   16, PlaybackState::StoppedState },
            {   17, PlaybackState::PlayingState },
            {   18, PlaybackState::PausedState },
        }),
        // enum 'MediaStatus'
        QtMocHelpers::EnumData<enum MediaStatus>(19, 19, QMC::EnumFlags{}).add({
            {   20, MediaStatus::NoMedia },
            {   21, MediaStatus::LoadedMedia },
            {   22, MediaStatus::BufferedMedia },
            {   23, MediaStatus::EndOfMedia },
            {   24, MediaStatus::InvalidMedia },
        }),
    };
    return QtMocHelpers::metaObjectData<GstPlayerBackend, qt_meta_tag_ZN16GstPlayerBackendE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject GstPlayerBackend::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16GstPlayerBackendE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16GstPlayerBackendE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN16GstPlayerBackendE_t>.metaTypes,
    nullptr
} };

void GstPlayerBackend::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<GstPlayerBackend *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->durationChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 1: _t->positionChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 2: _t->playbackStateChanged((*reinterpret_cast<std::add_pointer_t<GstPlayerBackend::PlaybackState>>(_a[1]))); break;
        case 3: _t->mediaStatusChanged((*reinterpret_cast<std::add_pointer_t<GstPlayerBackend::MediaStatus>>(_a[1]))); break;
        case 4: _t->metaDataChanged(); break;
        case 5: _t->volumeChanged((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (GstPlayerBackend::*)(qint64 )>(_a, &GstPlayerBackend::durationChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (GstPlayerBackend::*)(qint64 )>(_a, &GstPlayerBackend::positionChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (GstPlayerBackend::*)(GstPlayerBackend::PlaybackState )>(_a, &GstPlayerBackend::playbackStateChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (GstPlayerBackend::*)(GstPlayerBackend::MediaStatus )>(_a, &GstPlayerBackend::mediaStatusChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (GstPlayerBackend::*)()>(_a, &GstPlayerBackend::metaDataChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (GstPlayerBackend::*)(double )>(_a, &GstPlayerBackend::volumeChanged, 5))
            return;
    }
}

const QMetaObject *GstPlayerBackend::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *GstPlayerBackend::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16GstPlayerBackendE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int GstPlayerBackend::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void GstPlayerBackend::durationChanged(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void GstPlayerBackend::positionChanged(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void GstPlayerBackend::playbackStateChanged(GstPlayerBackend::PlaybackState _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void GstPlayerBackend::mediaStatusChanged(GstPlayerBackend::MediaStatus _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void GstPlayerBackend::metaDataChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void GstPlayerBackend::volumeChanged(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}
QT_WARNING_POP
