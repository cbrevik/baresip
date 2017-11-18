/**
 * @file mqtt.c Message Queue Telemetry Transport (MQTT) client
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <mosquitto.h>
#include <re.h>
#include <baresip.h>


static const char *broker_host = "127.0.0.1";
static const int broker_port = 1883;
static const char topic[] = "baresip";


static struct mqtt {
	struct mosquitto *mosq;
	struct tmr tmr;
} s_mqtt;


/* XXX: use mosquitto_socket and fd_listen instead? */
static void tmr_handler(void *data)
{
	struct mqtt *mqtt = data;
	const int timeout_ms = 50;
	int ret;

	ret = mosquitto_loop(mqtt->mosq, timeout_ms, 1);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: error in loop (%s)\n", mosquitto_strerror(ret));
	}

	tmr_start(&mqtt->tmr, 500, tmr_handler, mqtt);
}


/*
 * This is called when the broker sends a CONNACK message
 * in response to a connection.
 */
static void connect_callback(struct mosquitto *mosq, void *obj, int result)
{
	struct mqtt *mqtt = obj;
	int ret;

	if (result != MOSQ_ERR_SUCCESS) {
		warning("mqtt: could not connect to broker (%s)\n",
			mosquitto_strerror(result));
		return;
	}

	info("mqtt: connected to broker at %s:%d\n",
	     broker_host, broker_port);

	ret = mosquitto_subscribe(mosq, NULL, topic, 0);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: failed to subscribe (%s)\n",
			mosquitto_strerror(ret));
		return;
	}

	info("mqtt: subscribed to topic '%s'\n", topic);

#if 1
	/* XXX: for testing */
	ret = mosquitto_publish(mqtt->mosq,
				NULL,
				topic,
				5,
				"ready",
				0,
				false);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: failed to publish (%s)\n",
			mosquitto_strerror(ret));
		return;
	}
#endif
}


/*
 * This is called when a message is received from the broker.
 */
static void message_callback(struct mosquitto *mosq, void *obj,
			     const struct mosquitto_message *message)
{
	bool match = false;

	info("mqtt: got message '%b' for topic '%s'\n",
	     (char*) message->payload, (size_t)message->payloadlen,
	     message->topic);

	mosquitto_topic_matches_sub(topic, message->topic, &match);
	if (match) {
		info("mqtt: got message for '%s' topic\n", topic);

		/* XXX: handle message */
	}
}


static int module_init(void)
{
	const int keepalive = 60;
	int ret;
	int err = 0;

	tmr_init(&s_mqtt.tmr);

	mosquitto_lib_init();

	s_mqtt.mosq = mosquitto_new("baresip", true, &s_mqtt);
	if (!s_mqtt.mosq) {
		warning("mqtt: failed to create client instance\n");
		return ENOMEM;
	}

	mosquitto_connect_callback_set(s_mqtt.mosq, connect_callback);
	mosquitto_message_callback_set(s_mqtt.mosq, message_callback);

	ret = mosquitto_connect(s_mqtt.mosq, broker_host, broker_port,
				keepalive);
	if (ret != MOSQ_ERR_SUCCESS) {

		err = ret == MOSQ_ERR_ERRNO ? errno : EIO;

		warning("mqtt: failed to connect to %s:%d (%s)\n",
			broker_host, broker_port,
			mosquitto_strerror(ret));
		return err;
	}

	tmr_start(&s_mqtt.tmr, 1, tmr_handler, &s_mqtt);

	info("mqtt: module loaded\n");

	return err;
}


static int module_close(void)
{
	tmr_cancel(&s_mqtt.tmr);

	if (s_mqtt.mosq) {

		mosquitto_disconnect(s_mqtt.mosq);

		mosquitto_destroy(s_mqtt.mosq);
		s_mqtt.mosq = NULL;
	}

	mosquitto_lib_cleanup();

	info("mqtt: module unloaded\n");

	return 0;
}


const struct mod_export DECL_EXPORTS(mqtt) = {
	"mqtt",
	"application",
	module_init,
	module_close
};
