/* This file is part of scrab which is
 * licensed under the GPL v2.0
 * github.com/univrsal/scrab
 */
#include "plugin-macros.generated.h"
#include "screenshot/screenshotgrabber.hpp"
#include <QDate>
#include <QDir>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#define utf8_to_qt(_str) QString::fromUtf8(_str)
#define qt_to_utf8(_str) _str.toUtf8().constData()

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("scrab", "en-US")

#define S_HOTKEY_CAPTURE "scrab.hotkey.capture"
#define S_HOTKEY_RECAPTURE "scrab.hotkey.recapture"
#define S_CONTINOUS_MODE "continous"
#define S_PICTURE_FOLDER "image_folder"
#define S_PRIMARY_SCREEN "primary_screen"

#define T_(t) obs_module_text(t)
#define T_HOTKEY_CAPTURE_DESC T_("scrab.hotkey.capture.description")
#define T_HOTKEY_RECAPTURE_DESC T_("scrab.hotkey.recapture.description")

#define SRC_NAME "scrab-cap"

static QRect previous_region;
static obs_hotkey_id capture_key;
static obs_hotkey_id recapture_key;
static bool continous_mode = false;
static int source_counter = 1;
static const char* picture_folder = nullptr;

/* Gets the scrab source or creates it
 * and sets the file path to the provided one*/
obs_source_t* get_cap_source(const char* path)
{
    QString name(SRC_NAME);
    obs_source_t* src = nullptr;
    if (continous_mode) {
        /* find next free source name */
        for (;;) {
            QString tmp = name + " " + QString::number(source_counter++);
            src = obs_get_source_by_name(qt_to_utf8(tmp));
            if (!src) {
                name = tmp;
                break;
            }
            if (source_counter > 256) { /* Wtf? */
                blog(LOG_ERROR, "[scrab] image counter exceeded 256");
                obs_source_release(src);
                break;
            }
            obs_source_release(src);
        }
    } else {
        src = obs_get_source_by_name(qt_to_utf8(name));
    }

    obs_data_t* settings;
    if (src) {
        settings = obs_source_get_settings(src);
    } else {
        settings = obs_data_create();
        src = obs_source_create("image_source", qt_to_utf8(name), settings, nullptr);
    }
    obs_data_set_string(settings, "file", path);
    obs_source_update(src, settings);
    obs_data_release(settings);
    return src;
}

void setup_config()
{
    config_t* cfg = obs_frontend_get_global_config();
    config_set_default_string(cfg, "scrab", S_PICTURE_FOLDER,
        qt_to_utf8(QDir::homePath()));
    config_set_default_int(cfg, "scrab", S_PRIMARY_SCREEN, -1);
    config_set_default_int(cfg, "scrab", "x", 0);
    config_set_default_int(cfg, "scrab", "y", 0);
    config_set_default_int(cfg, "scrab", "w", 0);
    config_set_default_int(cfg, "scrab", "h", 0);
    config_set_default_bool(cfg, "scrab", S_CONTINOUS_MODE, false);
    picture_folder = config_get_string(cfg, "scrab", "image_folder");
    continous_mode = config_get_bool(cfg, "scrab", S_CONTINOUS_MODE);

    previous_region = QRect(
        static_cast<int>(config_get_int(cfg, "scrab", "x")),
        static_cast<int>(config_get_int(cfg, "scrab", "y")),
        static_cast<int>(config_get_int(cfg, "scrab", "w")),
        static_cast<int>(config_get_int(cfg, "scrab", "h")));
}

/* Saves a pixmap into the plugin folder with timestamp
 * and returns the path in path_out */
bool save_pixmap(QPixmap* pixmap, QString& path_out)
{
    bool result = true;
    QString file_name = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss.png");
    path_out = QDir::toNativeSeparators(QDir(picture_folder).absoluteFilePath(file_name));

    blog(LOG_DEBUG, "[scrab] Filename: %s", qt_to_utf8(file_name));
    blog(LOG_DEBUG, "[scrab] File path: %s", qt_to_utf8(path_out));

    if (!pixmap->save(path_out, "PNG")) {
        blog(LOG_ERROR, "[scrab] Couldn't save screenshot to %s", qt_to_utf8(path_out));
        result = false;
    }
    return result;
}

void screenshot_callback(bool result, QPixmap* arg, const QRect& rect)
{
    QString path;
    previous_region = rect;

    if (result && arg && save_pixmap(arg, path)) {
        /* Set image source */
        auto* scene_src = obs_frontend_get_current_scene();
        auto* scene = obs_scene_from_source(scene_src);
        auto* src = get_cap_source(qt_to_utf8(path));

        if (scene && src) {
            if (!obs_scene_find_source(scene, SRC_NAME)) {
                /* The current scene doesn't have the source yet */
                obs_scene_add(scene, src);
            }
        }

        if (scene_src)
            obs_source_release(scene_src);
        if (src)
            obs_source_release(src);

        config_t* cfg = obs_frontend_get_global_config();
        config_set_int(cfg, "scrab", "x", rect.x());
        config_set_int(cfg, "scrab", "y", rect.y());
        config_set_int(cfg, "scrab", "w", rect.width());
        config_set_int(cfg, "scrab", "h", rect.height());
    }
}

void capture_key_callback(void* data, obs_hotkey_id id, obs_hotkey_t* key,
    bool pressed)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(key);
    if (id != capture_key || !pressed)
        return;
    /* Capture screenshot, screenshotgrabber is a QObject and is automatically
     * deleted */

    auto* screen_grabber = new ScreenshotGrabber(&screenshot_callback);
    screen_grabber->showGrabber();
}

void recapture_key_callback(void* data, obs_hotkey_id id, obs_hotkey_t* key,
    bool pressed)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(key);
    if (id != recapture_key || !pressed)
        return;

    if (previous_region.height() <= 0 || previous_region.width() <= 0) {
        capture_key_callback(data, capture_key, nullptr, true);
        return;
    }

    /* Capture screenshot, screenshotgrabber is a QObject and is automatically
     * deleted */

    new ScreenshotGrabber(&screenshot_callback, previous_region);
}

void scrab_save(obs_data_t* save_data, bool saving, void* unused)
{
    obs_data_t* data = nullptr;
    obs_data_array_t *capture = nullptr, *recapture = nullptr;
    UNUSED_PARAMETER(unused);

    if (saving) {
        data = obs_data_create();
        capture = obs_hotkey_save(capture_key);
        recapture = obs_hotkey_save(recapture_key);
        obs_data_set_array(data, "capture_hotkey", capture);
        obs_data_set_array(data, "recapture_hotkey", recapture);
        obs_data_set_obj(save_data, "scrab", data);
        obs_data_array_release(capture);
        obs_data_array_release(recapture);
        obs_data_release(data);
    } else {
        data = obs_data_get_obj(save_data, "scrab");
        if (!data)
            data = obs_data_create();
        capture = obs_data_get_array(data, "capture_hotkey");
        recapture = obs_data_get_array(data, "recapture_hotkey");
        obs_hotkey_load(capture_key, capture);
        obs_hotkey_load(recapture_key, recapture);
        obs_data_array_release(capture);
        obs_data_array_release(recapture);
        obs_data_release(data);
    }
}

bool obs_module_load()
{
    blog(LOG_INFO, "[%s] Loading v%s", PLUGIN_NAME, PLUGIN_VERSION);
    setup_config();
    capture_key = obs_hotkey_register_frontend(S_HOTKEY_CAPTURE, T_HOTKEY_CAPTURE_DESC,
        capture_key_callback, nullptr);
    recapture_key = obs_hotkey_register_frontend(S_HOTKEY_RECAPTURE, T_HOTKEY_RECAPTURE_DESC,
        recapture_key_callback, nullptr);
    obs_frontend_add_save_callback(&scrab_save, nullptr);
    return true;
}

void obs_module_unload()
{
    /* NO-OP */
}
