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


/* XXX: relay the response as a message ? */
static int stdout_handler(const char *p, size_t size, void *arg)
{
	struct mqtt *mqtt = arg;
	(void)mqtt;

	if (1 != fwrite(p, size, 1, stdout))
		return ENOMEM;

	return 0;
}


static int publish_message(struct mqtt *mqtt, const char *message)
{
	int ret;

	ret = mosquitto_publish(mqtt->mosq,
				NULL,
				topic,
				(int)str_len(message),
				message,
				0,
				false);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: failed to publish (%s)\n",
			mosquitto_strerror(ret));
		return EINVAL;
	}

	return 0;
}


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
	struct mqtt *mqtt = obj;
	struct re_printf pf_stdout = {stdout_handler, mqtt};
	struct pl msg;
	bool match = false;

	info("mqtt: got message '%b' for topic '%s'\n",
	     (char*) message->payload, (size_t)message->payloadlen,
	     message->topic);

	msg.p = message->payload;
	msg.l = message->payloadlen;

	mosquitto_topic_matches_sub(topic, message->topic, &match);
	if (match) {

		info("mqtt: got message for '%s' topic\n", topic);

		/* XXX: handle message */

		if (msg.l > 1 && msg.p[0] == '/') {

			/* Relay message to long commands */
			cmd_process_long(baresip_commands(),
					 &msg.p[1],
					 msg.l - 1,
					 &pf_stdout, NULL);

		}
		else {
			info("mqtt: message not handled (%r)\n", &msg);
		}
	}
}


/*
 * Relay UA events as publish messages to the Broker
 */
static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct mqtt *mqtt = arg;
	const char *event_str = uag_event_str(ev);

	publish_message(mqtt, event_str);
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

	err = uag_event_register(ua_event_handler, &s_mqtt);
	if (err)
		return err;

	info("mqtt: module loaded\n");

	return err;
}


static int module_close(void)
{
	uag_event_unregister(&ua_event_handler);

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
