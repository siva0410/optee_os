// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2015-2019, Arm Limited and Contributors. All rights reserved.
 * Copyright (c) 2020, Linaro Limited
 */
#include <assert.h>
#include <config.h>
#include <confine_array_index.h>
#include <drivers/scmi-msg.h>
#include <drivers/scmi.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "voltage_domain.h"

static bool message_id_is_supported(unsigned int message_id);

size_t __weak plat_scmi_voltd_count(unsigned int channel_id __unused)
{
	return 0;
}

const char __weak *plat_scmi_voltd_get_name(unsigned int channel_id __unused,
					    unsigned int scmi_id __unused)
{
	return NULL;
}

int32_t __weak plat_scmi_voltd_levels_array(unsigned int channel_id __unused,
					    unsigned int scmi_id __unused,
					    size_t start_index __unused,
					    long *levels __unused,
					    size_t *nb_elts __unused)
{
	return SCMI_NOT_SUPPORTED;
}

int32_t __weak plat_scmi_voltd_levels_by_step(unsigned int channel_id __unused,
					      unsigned int scmi_id __unused,
					      long *steps __unused)
{
	return SCMI_NOT_SUPPORTED;
}

long __weak plat_scmi_voltd_get_level(unsigned int channel_id __unused,
				      unsigned int scmi_id __unused)
{
	return 0;
}

int32_t __weak plat_scmi_voltd_set_level(unsigned int channel_id __unused,
					 unsigned int scmi_id __unused,
					 long microvolt __unused)
{
	return SCMI_NOT_SUPPORTED;
}

int32_t __weak plat_scmi_voltd_get_config(unsigned int channel_id __unused,
					  unsigned int scmi_id __unused,
					  uint32_t *config __unused)
{
	return SCMI_NOT_SUPPORTED;
}

int32_t __weak plat_scmi_voltd_set_config(unsigned int channel_id __unused,
					  unsigned int scmi_id __unused,
					  uint32_t config __unused)
{
	return SCMI_NOT_SUPPORTED;
}

static void report_version(struct scmi_msg *msg)
{
	struct scmi_protocol_version_p2a out_args = {
		.status = SCMI_SUCCESS,
		.version = SCMI_PROTOCOL_VERSION_VOLTAGE_DOMAIN,
	};

	if (IS_ENABLED(CFG_SCMI_MSG_STRICT_ABI) && msg->in_size) {
		scmi_status_response(msg, SCMI_PROTOCOL_ERROR);
		return;
	}

	scmi_write_response(msg, &out_args, sizeof(out_args));
}

static void report_attributes(struct scmi_msg *msg)
{
	size_t domain_count = plat_scmi_voltd_count(msg->channel_id);
	struct scmi_protocol_attributes_p2a out_args = {
		.status = SCMI_SUCCESS,
		.attributes = domain_count,
	};

	assert(!(domain_count & ~SCMI_VOLTAGE_DOMAIN_COUNT_MASK));

	if (IS_ENABLED(CFG_SCMI_MSG_STRICT_ABI) && msg->in_size) {
		scmi_status_response(msg, SCMI_PROTOCOL_ERROR);
		return;
	}

	scmi_write_response(msg, &out_args, sizeof(out_args));
}

static void report_message_attributes(struct scmi_msg *msg)
{
	struct scmi_protocol_message_attributes_a2p *in_args = (void *)msg->in;
	struct scmi_protocol_message_attributes_p2a out_args = {
		.status = SCMI_SUCCESS,
		/* For this protocol, attributes shall be zero */
		.attributes = 0,
	};

	if (msg->in_size != sizeof(*in_args)) {
		scmi_status_response(msg, SCMI_PROTOCOL_ERROR);
		return;
	}

	if (!message_id_is_supported(in_args->message_id)) {
		scmi_status_response(msg, SCMI_NOT_FOUND);
		return;
	}

	scmi_write_response(msg, &out_args, sizeof(out_args));
}

static void scmi_voltd_domain_attributes(struct scmi_msg *msg)
{
	const struct scmi_voltd_attributes_a2p *in_args = (void *)msg->in;
	struct scmi_voltd_attributes_p2a out_args = {
		.status = SCMI_SUCCESS,
	};
	const char *name = NULL;
	unsigned int domain_id = 0;

	if (msg->in_size != sizeof(*in_args)) {
		scmi_status_response(msg, SCMI_PROTOCOL_ERROR);
		return;
	}

	if (in_args->domain_id >= plat_scmi_voltd_count(msg->channel_id)) {
		scmi_status_response(msg, SCMI_INVALID_PARAMETERS);
		return;
	}

	domain_id = confine_array_index(in_args->domain_id,
					plat_scmi_voltd_count(msg->channel_id));

	name = plat_scmi_voltd_get_name(msg->channel_id, domain_id);
	if (!name) {
		scmi_status_response(msg, SCMI_NOT_FOUND);
		return;
	}

	COPY_NAME_IDENTIFIER(out_args.name, name);

	scmi_write_response(msg, &out_args, sizeof(out_args));
}

#define LEVELS_BY_ARRAY(_nb_rates, _rem_rates) \
	SCMI_VOLTAGE_DOMAIN_LEVELS_FLAGS((_nb_rates), \
					 SCMI_VOLTD_LEVELS_FORMAT_LIST, \
					 (_rem_rates))

#define LEVELS_BY_STEP \
	SCMI_VOLTAGE_DOMAIN_LEVELS_FLAGS(3, SCMI_VOLTD_LEVELS_FORMAT_RANGE, 0)

#define LEVEL_DESC_SIZE		sizeof(int32_t)

static void scmi_voltd_describe_levels(struct scmi_msg *msg)
{
	const struct scmi_voltd_describe_levels_a2p *in_args = (void *)msg->in;
	struct scmi_voltd_describe_levels_p2a out_args = { };
	int32_t status = SCMI_GENERIC_ERROR;
	unsigned int out_count = 0;
	unsigned int domain_id = 0;
	int32_t *out_levels = NULL;
	size_t nb_levels = 0;

	if (msg->in_size != sizeof(*in_args)) {
		status = SCMI_PROTOCOL_ERROR;
		goto out;
	}

	if (in_args->domain_id >= plat_scmi_voltd_count(msg->channel_id)) {
		status = SCMI_INVALID_PARAMETERS;
		goto out;
	}

	if (msg->out_size < sizeof(out_args)) {
		status = SCMI_INVALID_PARAMETERS;
		goto out;
	}
	assert(IS_ALIGNED_WITH_TYPE(msg->out + sizeof(out_args), int32_t));
	out_levels = (int32_t *)(uintptr_t)(msg->out + sizeof(out_args));

	domain_id = confine_array_index(in_args->domain_id,
					plat_scmi_voltd_count(msg->channel_id));

	/* Platform may support array rate description */
	status = plat_scmi_voltd_levels_array(msg->channel_id, domain_id, 0,
					      NULL, &nb_levels);
	if (status == SCMI_SUCCESS) {
		size_t avail_sz = msg->out_size - sizeof(out_args);
		unsigned int level_index = in_args->level_index;
		unsigned int remaining = 0;

		if (avail_sz < LEVEL_DESC_SIZE && nb_levels) {
			status = SCMI_PROTOCOL_ERROR;
			goto out;
		}

		while (avail_sz >= LEVEL_DESC_SIZE && level_index < nb_levels) {
			long plat_level = 0;
			size_t cnt = 1;

			status = plat_scmi_voltd_levels_array(msg->channel_id,
							      domain_id,
							      level_index,
							      &plat_level,
							      &cnt);
			if (status)
				goto out;

			*out_levels = plat_level;

			avail_sz -= LEVEL_DESC_SIZE;
			out_levels++;
			level_index++;
		}

		remaining = nb_levels - in_args->level_index;
		out_count = level_index - in_args->level_index;
		out_args.flags = LEVELS_BY_ARRAY(out_count, remaining);
	} else if (status == SCMI_NOT_SUPPORTED) {
		long triplet[3] = { 0, 0, 0 };

		if (msg->out_size < sizeof(out_args) + 3 * LEVEL_DESC_SIZE) {
			status = SCMI_PROTOCOL_ERROR;
			goto out;
		}

		/* Platform may support min/max/step triplet description */
		status =  plat_scmi_voltd_levels_by_step(msg->channel_id,
							 domain_id, triplet);
		if (status)
			goto out;

		out_levels[0] = triplet[0];
		out_levels[1] = triplet[1];
		out_levels[2] = triplet[2];

		out_count = 3;
		out_args.flags = LEVELS_BY_STEP;
	}

out:
	if (status) {
		scmi_status_response(msg, status);
	} else {
		out_args.status = SCMI_SUCCESS;
		memcpy(msg->out, &out_args, sizeof(out_args));
		msg->out_size_out = sizeof(out_args) +
				    out_count * LEVEL_DESC_SIZE;
	}
}

static void scmi_voltd_config_set(struct scmi_msg *msg)
{
	const struct scmi_voltd_config_set_a2p *in_args = (void *)msg->in;
	unsigned int domain_id = 0;
	unsigned long config = 0;
	int32_t status = SCMI_GENERIC_ERROR;

	if (msg->in_size != sizeof(*in_args)) {
		scmi_status_response(msg, SCMI_PROTOCOL_ERROR);
		return;
	}

	if (in_args->domain_id >= plat_scmi_voltd_count(msg->channel_id)) {
		scmi_status_response(msg, SCMI_INVALID_PARAMETERS);
		return;
	}

	domain_id = confine_array_index(in_args->domain_id,
					plat_scmi_voltd_count(msg->channel_id));

	config = in_args->config & SCMI_VOLTAGE_DOMAIN_CONFIG_MASK;

	status = plat_scmi_voltd_set_config(msg->channel_id, domain_id, config);

	scmi_status_response(msg, status);
}

static void scmi_voltd_config_get(struct scmi_msg *msg)
{
	const struct scmi_voltd_config_get_a2p *in_args = (void *)msg->in;
	struct scmi_voltd_config_get_p2a out_args = { };
	unsigned int domain_id = 0;

	if (msg->in_size != sizeof(*in_args)) {
		scmi_status_response(msg, SCMI_PROTOCOL_ERROR);
		return;
	}

	if (in_args->domain_id >= plat_scmi_voltd_count(msg->channel_id)) {
		scmi_status_response(msg, SCMI_INVALID_PARAMETERS);
		return;
	}

	domain_id = confine_array_index(in_args->domain_id,
					plat_scmi_voltd_count(msg->channel_id));

	if (plat_scmi_voltd_get_config(msg->channel_id, domain_id,
				       &out_args.config)) {
		scmi_status_response(msg, SCMI_INVALID_PARAMETERS);
		return;
	}

	scmi_write_response(msg, &out_args, sizeof(out_args));
}

static void scmi_voltd_level_set(struct scmi_msg *msg)
{
	const struct scmi_voltd_level_set_a2p *in_args = (void *)msg->in;
	int32_t status = SCMI_GENERIC_ERROR;
	unsigned int domain_id = 0;

	if (msg->in_size != sizeof(*in_args)) {
		scmi_status_response(msg, SCMI_PROTOCOL_ERROR);
		return;
	}

	if (in_args->domain_id >= plat_scmi_voltd_count(msg->channel_id)) {
		scmi_status_response(msg, SCMI_INVALID_PARAMETERS);
		return;
	}

	domain_id = confine_array_index(in_args->domain_id,
					plat_scmi_voltd_count(msg->channel_id));

	status = plat_scmi_voltd_set_level(msg->channel_id, domain_id,
					   in_args->voltage_level);

	scmi_status_response(msg, status);
}

static void scmi_voltd_level_get(struct scmi_msg *msg)
{
	const struct scmi_voltd_level_get_a2p *in_args = (void *)msg->in;
	struct scmi_voltd_level_get_p2a out_args = {
		.status = SCMI_SUCCESS,
	};
	unsigned int domain_id = 0;

	if (msg->in_size != sizeof(*in_args)) {
		scmi_status_response(msg, SCMI_PROTOCOL_ERROR);
		return;
	}

	if (in_args->domain_id >= plat_scmi_voltd_count(msg->channel_id)) {
		scmi_status_response(msg, SCMI_INVALID_PARAMETERS);
		return;
	}

	domain_id = confine_array_index(in_args->domain_id,
					plat_scmi_voltd_count(msg->channel_id));

	out_args.voltage_level = plat_scmi_voltd_get_level(msg->channel_id,
							   domain_id);

	scmi_write_response(msg, &out_args, sizeof(out_args));
}

static const scmi_msg_handler_t handler_array[] = {
	[SCMI_PROTOCOL_VERSION] = report_version,
	[SCMI_PROTOCOL_ATTRIBUTES] = report_attributes,
	[SCMI_PROTOCOL_MESSAGE_ATTRIBUTES] = report_message_attributes,
	[SCMI_VOLTAGE_DOMAIN_ATTRIBUTES] = scmi_voltd_domain_attributes,
	[SCMI_VOLTAGE_DESCRIBE_LEVELS] = scmi_voltd_describe_levels,
	[SCMI_VOLTAGE_CONFIG_SET] = scmi_voltd_config_set,
	[SCMI_VOLTAGE_CONFIG_GET] = scmi_voltd_config_get,
	[SCMI_VOLTAGE_LEVEL_SET] = scmi_voltd_level_set,
	[SCMI_VOLTAGE_LEVEL_GET] = scmi_voltd_level_get,
};

static bool message_id_is_supported(size_t id)
{
	return id < ARRAY_SIZE(handler_array) && handler_array[id];
}

scmi_msg_handler_t scmi_msg_get_voltd_handler(struct scmi_msg *msg)
{
	const size_t array_size = ARRAY_SIZE(handler_array);
	unsigned int message_id = 0;

	if (msg->message_id >= array_size) {
		DMSG("Voltage domain handle not found %u", msg->message_id);
		return NULL;
	}

	message_id = confine_array_index(msg->message_id, array_size);

	return handler_array[message_id];
}
