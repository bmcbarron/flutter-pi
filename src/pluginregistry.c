#ifdef __STDC_ALLOC_LIB__
#define __STDC_WANT_LIB_EXT2__ 1
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include <unistd.h>
#include <string.h>
#include <sys/select.h>

#include <platformchannel.h>
#include <pluginregistry.h>
#include <collection.h>

#include <plugins/services.h>
#include <plugins/raw_keyboard.h>
#include <plugins/firebase.h>

#ifdef BUILD_TEXT_INPUT_PLUGIN
#	include <plugins/text_input.h>
#endif
#ifdef BUILD_TEST_PLUGIN
#	include <plugins/testplugin.h>
#endif
#ifdef BUILD_GPIOD_PLUGIN
#	include <plugins/gpiod_plugin.h>
#endif
#ifdef BUILD_SPIDEV_PLUGIN
#	include <plugins/spidev.h>
#endif
#ifdef BUILD_OMXPLAYER_VIDEO_PLAYER_PLUGIN
#	include <plugins/omxplayer_video_player.h>
#endif


struct platch_obj_cb_data {
	char *channel;
	enum platch_codec codec;
	platch_obj_recv_callback callback;
	void *userdata;
};

struct plugin_registry {
	size_t n_plugins;
	struct flutterpi_plugin *plugins;

	// platch_obj callbacks
	struct concurrent_pointer_set platch_obj_cbs;
} plugin_registry;

/// array of plugins that are statically included in flutter-pi.
struct flutterpi_plugin hardcoded_plugins[] = {
	{.name = "services",     .init = services_init, .deinit = services_deinit},
	{.name = "raw_keyboard", .init = rawkb_init, .deinit = rawkb_deinit},

	{.name = "firebase", .init = firebase_init, .deinit = firebase_deinit},

#ifdef BUILD_TEXT_INPUT_PLUGIN
	{.name = "text_input",   .init = textin_init, .deinit = textin_deinit},
#endif

#ifdef BUILD_TEST_PLUGIN
	{.name = "testplugin",   .init = testp_init, .deinit = testp_deinit},
#endif

#ifdef BUILD_OMXPLAYER_VIDEO_PLAYER_PLUGIN
	{.name = "omxplayer_video_player", .init = omxpvidpp_init, .deinit = omxpvidpp_deinit},
#endif
};

static struct platch_obj_cb_data *plugin_registry_get_cb_data_by_channel_locked(const char *channel) {
	struct platch_obj_cb_data *data;

	for_each_pointer_in_cpset(&plugin_registry.platch_obj_cbs, data) {
		if (strcmp(data->channel, channel) == 0) {
			return data;
		}
	}

	return NULL;
}

static struct platch_obj_cb_data *plugin_registry_get_cb_data_by_channel(const char *channel) {
	struct platch_obj_cb_data *data;

	cpset_lock(&plugin_registry.platch_obj_cbs);
	data = plugin_registry_get_cb_data_by_channel_locked(channel);
	cpset_unlock(&plugin_registry.platch_obj_cbs);

	return data;
}

int plugin_registry_init() {
	int ok;

	plugin_registry.n_plugins = sizeof(hardcoded_plugins) / sizeof(*hardcoded_plugins);
	plugin_registry.plugins = hardcoded_plugins;
	plugin_registry.platch_obj_cbs = CPSET_INITIALIZER(CPSET_DEFAULT_MAX_SIZE);

	for (int i = 0; i < plugin_registry.n_plugins; i++) {
		if (plugin_registry.plugins[i].init != NULL) {
			ok = plugin_registry.plugins[i].init();
			if (ok != 0) {
				fprintf(stderr, "[plugin registry] Could not initialize plugin %s. init: %s\n", plugin_registry.plugins[i].name, strerror(ok));
				return ok;
			}
		}
	}

	return 0;
}

int plugin_registry_on_platform_message(FlutterPlatformMessage *message) {
  fprintf(stderr, "plugin_registry_on_platform_message(%s)\n", message->channel);

	struct platch_obj_cb_data *data, data_copy;
	struct platch_obj object;
	int ok;

	cpset_lock(&plugin_registry.platch_obj_cbs);

	data = plugin_registry_get_cb_data_by_channel_locked(message->channel);
	if (data == NULL || data->callback == NULL) {
		cpset_unlock(&plugin_registry.platch_obj_cbs);
		return platch_respond_not_implemented((FlutterPlatformMessageResponseHandle*) message->response_handle);
	}

	data_copy = *data;
	cpset_unlock(&plugin_registry.platch_obj_cbs);

	ok = platch_decode((uint8_t*) message->message, message->message_size, data_copy.codec, &object);
	if (ok != 0) {
		fprintf(stderr, "platch_decode failed: %d\n", ok);
		return ok;
	}

	ok = data_copy.callback((char*) message->channel, &object, (FlutterPlatformMessageResponseHandle*) message->response_handle); //, data->userdata);
	if (ok != 0) {
		fprintf(stderr, "data_copy.callback failed: %d\n", ok);
		platch_free_obj(&object);
		return ok;
	}

	platch_free_obj(&object);

	return 0;
}

int plugin_registry_set_receiver(
	const char *channel,
	enum platch_codec codec,
	platch_obj_recv_callback callback
	//void *userdata
) {
  fprintf(stderr, "plugin_registry_set_receiver(%s)\n", channel);

	struct platch_obj_cb_data *data;
	char *channel_dup;

	cpset_lock(&plugin_registry.platch_obj_cbs);
	
	channel_dup = strdup(channel);
	if (channel_dup == NULL) {
		cpset_unlock(&plugin_registry.platch_obj_cbs);
		return ENOMEM;
	}

	data = plugin_registry_get_cb_data_by_channel_locked(channel);
	if (data == NULL) {
		data = calloc(1, sizeof *data);
		if (data == NULL) {
			free(channel_dup);
			cpset_unlock(&plugin_registry.platch_obj_cbs);
			return ENOMEM;
		}

		cpset_put_locked(&plugin_registry.platch_obj_cbs, data);
	}

	data->channel = channel_dup;
	data->codec = codec;
	data->callback = callback;
	//data->userdata = userdata;

	cpset_unlock(&plugin_registry.platch_obj_cbs);

	return 0;
}

bool plugin_registry_is_plugin_present(
	const char *plugin_name
) {
	fprintf(stderr, "plugin_registry_is_plugin_present(%s)\n", plugin_name);
	for (int i = 0; i < plugin_registry.n_plugins; i++) {
		if (strcmp(plugin_registry.plugins[i].name, plugin_name) == 0) {
			return true;
		}
	}

	return false;
}

int plugin_registry_remove_receiver(const char *channel) {
	struct platch_obj_cb_data *data;

	cpset_lock(&plugin_registry.platch_obj_cbs);

	data = plugin_registry_get_cb_data_by_channel_locked(channel);
	if (data == NULL) {
		cpset_unlock(&plugin_registry.platch_obj_cbs);
		return EINVAL;
	}

	cpset_remove_locked(&plugin_registry.platch_obj_cbs, data);

	free(data->channel);
	free(data);

	cpset_unlock(&plugin_registry.platch_obj_cbs);

	return 0;
}

int plugin_registry_deinit() {
	struct platch_obj_cb_data *data;
	int ok;
	
	/// call each plugins 'deinit'
	for (int i = 0; i < plugin_registry.n_plugins; i++) {
		if (plugin_registry.plugins[i].deinit) {
			ok = plugin_registry.plugins[i].deinit();
			if (ok != 0) {
				fprintf(stderr, "[plugin registry] Could not deinitialize plugin %s. deinit: %s\n", plugin_registry.plugins[i].name, strerror(ok));
			}
		}
	}

	for_each_pointer_in_cpset(&plugin_registry.platch_obj_cbs, data) {
		cpset_remove_(&plugin_registry.platch_obj_cbs, data);
		if (data != NULL) {
			free(data->channel);
			free(data);
		}
	}

	cpset_deinit(&plugin_registry.platch_obj_cbs);

	return 0;
}
