#include "gstplayerbackend.h"

#include <QTimer>

#include <gst/audio/streamvolume.h>

namespace {
constexpr int kBusPollMs = 40;
constexpr int kPositionPollMs = 200;
constexpr guint kPlayFlagSoftVolume = 0x00000010;

bool ensureGStreamerInitialized()
{
    static bool initialized = false;
    if (!initialized) {
        gst_init(nullptr, nullptr);
        initialized = true;
    }
    return initialized;
}

GstPlayerBackend::PlaybackState playbackStateFromGst(const GstState state, const bool hasSource)
{
    switch (state) {
    case GST_STATE_PLAYING:
        return GstPlayerBackend::PlayingState;
    case GST_STATE_PAUSED:
        return hasSource ? GstPlayerBackend::PausedState : GstPlayerBackend::StoppedState;
    case GST_STATE_READY:
    case GST_STATE_NULL:
    default:
        return GstPlayerBackend::StoppedState;
    }
}

qint64 gstClockTimeToMs(const gint64 value)
{
    if (value <= 0 || value == static_cast<gint64>(GST_CLOCK_TIME_NONE)) {
        return 0;
    }
    return static_cast<qint64>(value / GST_MSECOND);
}

void onBackendVolumeNotify(GObject *, GParamSpec *, gpointer userdata)
{
    if (auto *backend = static_cast<GstPlayerBackend *>(userdata)) {
        backend->handleVolumeNotify();
    }
}

void onDeepElementAdded(GstBin *, GstBin *, GstElement *element, gpointer userdata)
{
    if (auto *backend = static_cast<GstPlayerBackend *>(userdata)) {
        backend->handleDeepElementAdded(element);
    }
}

void onDeepElementRemoved(GstBin *, GstBin *, GstElement *element, gpointer userdata)
{
    if (auto *backend = static_cast<GstPlayerBackend *>(userdata)) {
        backend->handleDeepElementRemoved(element);
    }
}
}

GstPlayerBackend::GstPlayerBackend(QObject *parent)
    : QObject(parent)
{
    ensureGStreamerInitialized();

    m_playbin = gst_element_factory_make("playbin3", "deed-playbin");
    if (!m_playbin) {
        m_playbin = gst_element_factory_make("playbin", "deed-playbin");
    }

    if (m_playbin) {
        if (setupAudioBin()) {
            g_object_set(G_OBJECT(m_playbin), "audio-sink", m_audioBin, nullptr);
        }

        guint flags = 0;
        g_object_get(G_OBJECT(m_playbin), "flags", &flags, nullptr);
        flags &= ~kPlayFlagSoftVolume;
        g_object_set(G_OBJECT(m_playbin), "flags", flags, nullptr);

        m_bus = gst_element_get_bus(m_playbin);
    }

    m_busTimer = new QTimer(this);
    m_busTimer->setInterval(kBusPollMs);
    connect(m_busTimer, &QTimer::timeout, this, &GstPlayerBackend::drainBus);
    m_busTimer->start();

    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(kPositionPollMs);
    connect(m_positionTimer, &QTimer::timeout, this, &GstPlayerBackend::pollPositionAndDuration);
    m_positionTimer->start();
}

GstPlayerBackend::~GstPlayerBackend()
{
    disconnectVolumeNotifications();

    if (m_playbin) {
        gst_element_set_state(m_playbin, GST_STATE_NULL);
    }
    if (m_bus) {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }
    if (m_playbin) {
        gst_object_unref(m_playbin);
        m_playbin = nullptr;
    }

    clearAudioBin();
}

bool GstPlayerBackend::setupAudioBin()
{
    clearAudioBin();

    m_audioBin = gst_bin_new("deed-audio-bin");
    if (!m_audioBin) {
        return false;
    }

    GstElement *volume = gst_element_factory_make("volume", "deed-volume");
    GstElement *sink = gst_element_factory_make("pulsesink", "deed-pulse-sink");
    if (!sink) {
        sink = gst_element_factory_make("autoaudiosink", "deed-auto-sink");
    }

    if (!volume || !sink) {
        if (volume) {
            gst_object_unref(volume);
        }
        if (sink) {
            gst_object_unref(sink);
        }
        clearAudioBin();
        return false;
    }

    m_fallbackVolumeElement = volume;
    m_audioSink = sink;

    gst_bin_add_many(GST_BIN(m_audioBin), volume, sink, nullptr);
    if (!gst_element_link(volume, sink)) {
        clearAudioBin();
        return false;
    }

    GstPad *volumeSinkPad = gst_element_get_static_pad(volume, "sink");
    if (!volumeSinkPad) {
        clearAudioBin();
        return false;
    }

    GstPad *ghostPad = gst_ghost_pad_new("sink", volumeSinkPad);
    gst_object_unref(volumeSinkPad);
    if (!ghostPad) {
        clearAudioBin();
        return false;
    }

    if (!gst_element_add_pad(m_audioBin, ghostPad)) {
        gst_object_unref(ghostPad);
        clearAudioBin();
        return false;
    }

    m_deepElementAddedHandlerId = g_signal_connect(
        G_OBJECT(m_audioBin),
        "deep-element-added",
        G_CALLBACK(onDeepElementAdded),
        this
    );
    m_deepElementRemovedHandlerId = g_signal_connect(
        G_OBJECT(m_audioBin),
        "deep-element-removed",
        G_CALLBACK(onDeepElementRemoved),
        this
    );

    setControlledVolumeElement(m_fallbackVolumeElement);
    setFallbackVolumeValue(m_cachedVolume);

    if (GST_IS_STREAM_VOLUME(m_audioSink)) {
        setControlledVolumeElement(m_audioSink);
    }

    return true;
}

void GstPlayerBackend::clearAudioBin()
{
    disconnectVolumeNotifications();

    if (m_audioBin && m_deepElementAddedHandlerId != 0) {
        g_signal_handler_disconnect(G_OBJECT(m_audioBin), m_deepElementAddedHandlerId);
    }
    if (m_audioBin && m_deepElementRemovedHandlerId != 0) {
        g_signal_handler_disconnect(G_OBJECT(m_audioBin), m_deepElementRemovedHandlerId);
    }
    m_deepElementAddedHandlerId = 0;
    m_deepElementRemovedHandlerId = 0;

    if (m_audioBin) {
        gst_element_set_state(m_audioBin, GST_STATE_NULL);
        gst_object_unref(m_audioBin);
    }

    m_audioBin = nullptr;
    m_audioSink = nullptr;
    m_fallbackVolumeElement = nullptr;
    m_controlledVolumeElement = nullptr;
}

void GstPlayerBackend::connectVolumeNotifications()
{
    disconnectVolumeNotifications();

    if (!m_controlledVolumeElement) {
        return;
    }

    m_volumeNotifyHandlerId = g_signal_connect(
        G_OBJECT(m_controlledVolumeElement),
        "notify::volume",
        G_CALLBACK(onBackendVolumeNotify),
        this
    );

    handleVolumeNotify();
}

void GstPlayerBackend::disconnectVolumeNotifications()
{
    if (m_controlledVolumeElement && m_volumeNotifyHandlerId != 0) {
        g_signal_handler_disconnect(G_OBJECT(m_controlledVolumeElement), m_volumeNotifyHandlerId);
    }
    m_volumeNotifyHandlerId = 0;
}

void GstPlayerBackend::setControlledVolumeElement(GstElement *element)
{
    if (!element) {
        element = m_fallbackVolumeElement;
    }

    if (m_controlledVolumeElement == element) {
        return;
    }

    disconnectVolumeNotifications();
    m_controlledVolumeElement = element;

    if (m_controlledVolumeElement != m_fallbackVolumeElement && m_fallbackVolumeElement) {
        setFallbackVolumeValue(1.0);
    } else {
        setFallbackVolumeValue(m_cachedVolume);
    }

    connectVolumeNotifications();
}

void GstPlayerBackend::setFallbackVolumeValue(const double volume)
{
    if (!m_fallbackVolumeElement) {
        return;
    }

    const gdouble gvolume = static_cast<gdouble>(volume);
    g_object_set(G_OBJECT(m_fallbackVolumeElement), "volume", gvolume, nullptr);
}

void GstPlayerBackend::handleDeepElementAdded(GstElement *element)
{
    if (!element || element == m_fallbackVolumeElement) {
        return;
    }

    if (GST_IS_STREAM_VOLUME(element)) {
        setControlledVolumeElement(element);
    }
}

void GstPlayerBackend::handleDeepElementRemoved(GstElement *element)
{
    if (!element) {
        return;
    }

    if (element == m_controlledVolumeElement && element != m_fallbackVolumeElement) {
        setControlledVolumeElement(m_fallbackVolumeElement);
    }
}

void GstPlayerBackend::resetPositionAndDuration()
{
    const bool positionChangedNeeded = (m_positionMs != 0);
    const bool durationChangedNeeded = (m_durationMs != 0);

    m_positionMs = 0;
    m_durationMs = 0;

    if (positionChangedNeeded) {
        emit positionChanged(0);
    }
    if (durationChangedNeeded) {
        emit durationChanged(0);
    }
}

void GstPlayerBackend::setSource(const QUrl &url)
{
    if (!m_playbin) {
        return;
    }

    gst_element_set_state(m_playbin, GST_STATE_READY);

    m_source = url;
    resetPositionAndDuration();
    emit metaDataChanged();

    if (url.isEmpty()) {
        updatePlaybackState(StoppedState);
        updateMediaStatus(NoMedia);
        return;
    }

    const QByteArray uri = url.toString().toUtf8();
    g_object_set(G_OBJECT(m_playbin), "uri", uri.constData(), nullptr);

    updatePlaybackState(StoppedState);
    updateMediaStatus(LoadedMedia);
    syncPlaybackStateFromPipeline();
}

bool GstPlayerBackend::requestPipelineState(GstState state)
{
    if (!m_playbin) {
        return false;
    }

    const GstStateChangeReturn result = gst_element_set_state(m_playbin, state);
    if (result == GST_STATE_CHANGE_FAILURE) {
        updatePlaybackState(StoppedState);
        updateMediaStatus(InvalidMedia);
        return false;
    }

    syncPlaybackStateFromPipeline();
    return true;
}

void GstPlayerBackend::play()
{
    if (!m_playbin || m_source.isEmpty()) {
        return;
    }

    requestPipelineState(GST_STATE_PLAYING);
}

void GstPlayerBackend::pause()
{
    if (!m_playbin || m_source.isEmpty()) {
        return;
    }

    requestPipelineState(GST_STATE_PAUSED);
}

void GstPlayerBackend::stop()
{
    if (!m_playbin) {
        return;
    }

    if (!requestPipelineState(GST_STATE_READY)) {
        return;
    }

    if (m_positionMs != 0) {
        m_positionMs = 0;
        emit positionChanged(0);
    }
    updatePlaybackState(StoppedState);
}

void GstPlayerBackend::setPosition(qint64 positionMs)
{
    if (!m_playbin || m_source.isEmpty()) {
        return;
    }

    if (positionMs < 0) {
        positionMs = 0;
    } else if (m_durationMs > 0 && positionMs > m_durationMs) {
        positionMs = m_durationMs;
    }

    if (gst_element_seek_simple(
            m_playbin,
            GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            static_cast<gint64>(positionMs) * GST_MSECOND)) {
        if (m_positionMs != positionMs) {
            m_positionMs = positionMs;
            emit positionChanged(m_positionMs);
        }
    }
}

qint64 GstPlayerBackend::position() const
{
    return m_positionMs;
}

qint64 GstPlayerBackend::duration() const
{
    return m_durationMs;
}

void GstPlayerBackend::setVolume(double volume)
{
    if (!m_controlledVolumeElement || !GST_IS_STREAM_VOLUME(m_controlledVolumeElement)) {
        return;
    }

    if (volume < 0.0) {
        volume = 0.0;
    } else if (volume > 1.0) {
        volume = 1.0;
    }

    gst_stream_volume_set_volume(
        GST_STREAM_VOLUME(m_controlledVolumeElement),
        GST_STREAM_VOLUME_FORMAT_LINEAR,
        static_cast<gdouble>(volume)
    );

    m_cachedVolume = volume;
    emit volumeChanged(m_cachedVolume);
}

double GstPlayerBackend::volume() const
{
    if (!m_controlledVolumeElement || !GST_IS_STREAM_VOLUME(m_controlledVolumeElement)) {
        return m_cachedVolume;
    }

    const gdouble value = gst_stream_volume_get_volume(
        GST_STREAM_VOLUME(m_controlledVolumeElement),
        GST_STREAM_VOLUME_FORMAT_LINEAR
    );
    return static_cast<double>(value);
}

GstPlayerBackend::PlaybackState GstPlayerBackend::playbackState() const
{
    return m_playbackState;
}

void GstPlayerBackend::handleVolumeNotify()
{
    const double newVolume = volume();
    if (qAbs(m_cachedVolume - newVolume) < 0.0001) {
        return;
    }

    m_cachedVolume = newVolume;
    emit volumeChanged(m_cachedVolume);
}

void GstPlayerBackend::drainBus()
{
    if (!m_bus) {
        return;
    }

    while (GstMessage *message = gst_bus_pop(m_bus)) {
        handleMessage(message);
        gst_message_unref(message);
    }
}

void GstPlayerBackend::handleMessage(GstMessage *message)
{
    if (!message || !m_playbin) {
        return;
    }

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
        gst_element_set_state(m_playbin, GST_STATE_READY);
        if (m_positionMs != 0) {
            m_positionMs = 0;
            emit positionChanged(0);
        }
        updatePlaybackState(StoppedState);
        updateMediaStatus(EndOfMedia);
        break;

    case GST_MESSAGE_ERROR:
        updatePlaybackState(StoppedState);
        updateMediaStatus(InvalidMedia);
        gst_element_set_state(m_playbin, GST_STATE_READY);
        break;

    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(m_playbin)) {
            GstState oldState = GST_STATE_NULL;
            GstState newState = GST_STATE_NULL;
            GstState pendingState = GST_STATE_NULL;
            gst_message_parse_state_changed(message, &oldState, &newState, &pendingState);
            Q_UNUSED(oldState)
            Q_UNUSED(pendingState)

            updatePlaybackState(playbackStateFromGst(newState, !m_source.isEmpty()));

            if ((newState == GST_STATE_PAUSED || newState == GST_STATE_PLAYING) && !m_source.isEmpty()) {
                updateMediaStatus(BufferedMedia);
            } else if ((newState == GST_STATE_READY || newState == GST_STATE_NULL) && m_source.isEmpty()) {
                updateMediaStatus(NoMedia);
            }
        }
        break;

    case GST_MESSAGE_DURATION_CHANGED:
        pollPositionAndDuration();
        break;

    default:
        break;
    }
}

void GstPlayerBackend::pollPositionAndDuration()
{
    if (!m_playbin) {
        return;
    }

    syncPlaybackStateFromPipeline();

    if (m_source.isEmpty()) {
        return;
    }

    gint64 pos = GST_CLOCK_TIME_NONE;
    if (gst_element_query_position(m_playbin, GST_FORMAT_TIME, &pos)) {
        const qint64 posMs = gstClockTimeToMs(pos);
        if (posMs != m_positionMs) {
            m_positionMs = posMs;
            emit positionChanged(m_positionMs);
        }
    }

    gint64 dur = GST_CLOCK_TIME_NONE;
    if (gst_element_query_duration(m_playbin, GST_FORMAT_TIME, &dur)) {
        const qint64 durMs = gstClockTimeToMs(dur);
        if (durMs != m_durationMs) {
            m_durationMs = durMs;
            emit durationChanged(m_durationMs);
        }
    }
}

void GstPlayerBackend::updatePlaybackState(PlaybackState state)
{
    if (m_playbackState == state) {
        return;
    }

    m_playbackState = state;
    emit playbackStateChanged(m_playbackState);
}

void GstPlayerBackend::updateMediaStatus(MediaStatus status)
{
    if (m_mediaStatus == status) {
        return;
    }

    m_mediaStatus = status;
    emit mediaStatusChanged(m_mediaStatus);
}

void GstPlayerBackend::syncPlaybackStateFromPipeline()
{
    if (!m_playbin) {
        return;
    }

    GstState currentState = GST_STATE_NULL;
    GstState pendingState = GST_STATE_NULL;
    const GstStateChangeReturn result = gst_element_get_state(m_playbin, &currentState, &pendingState, 0);

    if (result == GST_STATE_CHANGE_FAILURE) {
        updatePlaybackState(StoppedState);
        updateMediaStatus(InvalidMedia);
        return;
    }

    if (result == GST_STATE_CHANGE_SUCCESS || result == GST_STATE_CHANGE_ASYNC || result == GST_STATE_CHANGE_NO_PREROLL) {
        Q_UNUSED(pendingState)
        updatePlaybackState(playbackStateFromGst(currentState, !m_source.isEmpty()));
    }
}
