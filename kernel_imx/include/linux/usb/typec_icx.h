/*
 * typec_icx.h
 *
 * Copyright 2017, 2018 Sony Video & Sound Products Inc.
 * Author: Sony Video & Sound Products Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __TYPEC_ICX_H__
#define __TYPEC_ICX_H__

#define TYPEC_ICX_NO_AUTO_SINK_VOLTAGE	(~((u32)0))

/* Attached cable(connected to port) type */
#define TYPEC_ICX_CONNECTED_TO_NONE		(0)
#define TYPEC_ICX_CONNECTED_TO_SOURCE		(1) /* Attached.SNK */
#define TYPEC_ICX_CONNECTED_TO_SINK		(2) /* Attached.SRC */
#define TYPEC_ICX_CONNECTED_TO_SOURCE_DEBUG	(3) /* DebugAccessory.SNK */
#define TYPEC_ICX_CONNECTED_TO_SINK_DEBUG	(4) /* DebugAccessory.SRC */
#define TYPEC_ICX_CONNECTED_TO_AUDIO_VBUS	(5) /* AudioAccessory w VBUS */
#define TYPEC_ICX_CONNECTED_TO_AUDIO		(6) /* AudioAccessory wo VBUS */
#define TYPEC_ICX_CONNECTED_TO_UNKNOWN		(7)
#define TYPEC_ICX_CONNECTED_TO_NUMS		(8)

/* Source/Sink current. */
#define TYPEC_ICX_CURRENT_0MA		(0)
#define TYPEC_ICX_CURRENT_500MA		(500)
#define TYPEC_ICX_CURRENT_1500MA	(1500)
#define TYPEC_ICX_CURRENT_3000MA	(3000)
#define TYPEC_ICX_CURRENT_NO_MA		(TYPEC_ICX_CURRENT_0MA)
#define TYPEC_ICX_CURRENT_STANDARD_MA	(TYPEC_ICX_CURRENT_500MA)

/* Port Role Configuration. */
#define TYPEC_ICX_PORT_ROLE_NOTHING	(-1) /* DT property Nothing ( < 0) */
#define TYPEC_ICX_PORT_ROLE_KEEP	(0) /* Keep Role */
#define TYPEC_ICX_PORT_ROLE_SINK	(1) /* Sink Role */
#define TYPEC_ICX_PORT_ROLE_SOURCE	(2) /* Source Role */
#define TYPEC_ICX_PORT_ROLE_DRP		(3) /* Dual Role */
#define TYPEC_ICX_PORT_ROLE_DRP_TRY_SNK	(4) /* Dual Role with Try.Snk */
#define TYPEC_ICX_PORT_ROLE_DRP_TRY_SRC	(5) /* Dual Role with Try.Src */
#define TYPEC_ICX_PORT_ROLE_UNKNOWN	(6) /* Unknown */
#define TYPEC_ICX_PORT_ROLE_NUMS	(7)
#define TYPEC_ICX_PORT_ROLE_VALID_MAX	(5)

#endif /* __TYPEC_ICX_H__ */
