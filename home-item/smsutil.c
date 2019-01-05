/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2010  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>

#include "util.h"
#include "smsutil.h"

#define uninitialized_var(x) x = x

static gboolean verify_udh(const guint8 *hdr, guint8 max_len)
{
	guint8 max_offset;
	guint8 offset;

	/* Must have at least one information-element if udhi is true */
	if (hdr[0] < 2)
		return FALSE;

	if (hdr[0] >= max_len)
		return FALSE;

	/*
	 * According to 23.040: If the length of the User Data Header is
	 * such that there are too few or too many octets in the final
	 * Information Element then the whole User Data Header shall be
	 * ignored.
	 */

	max_offset = hdr[0] + 1;
	offset = 1;
	do {
		if ((offset + 2) > max_offset)
			return FALSE;

		if ((offset + 2 + hdr[offset + 1]) > max_offset)
			return FALSE;

		offset = offset + 2 + hdr[offset + 1];
	} while (offset < max_offset);

	if (offset != max_offset)
		return FALSE;

	return TRUE;
}

enum sms_iei sms_udh_iter_get_ie_type(struct sms_udh_iter *iter)
{
	if (iter->offset > iter->data[0])
		return SMS_IEI_INVALID;

	return (enum sms_iei) iter->data[iter->offset];
}

guint8 sms_udh_iter_get_ie_length(struct sms_udh_iter *iter)
{
	guint8 ie_len;

	ie_len = iter->data[iter->offset + 1];

	return ie_len;
}

void sms_udh_iter_get_ie_data(struct sms_udh_iter *iter, guint8 *data)
{
	guint8 ie_len;

	ie_len = iter->data[iter->offset + 1];

	memcpy(data, &iter->data[iter->offset + 2], ie_len);
}

gboolean sms_udh_iter_next(struct sms_udh_iter *iter)
{
	if (iter->offset > iter->data[0])
		return FALSE;

	iter->offset = iter->offset + 2 + iter->data[iter->offset + 1];

	if (iter->offset > iter->data[0])
		return FALSE;

	return TRUE;
}

static inline int sms_text_capacity_gsm(int max, int offset)
{
	return max - (offset * 8 + 6) / 7;
}

static gboolean extract_app_port_common(struct sms_udh_iter *iter, int *dst,
					int *src, gboolean *is_8bit)
{
	enum sms_iei iei;
	guint8 addr_hdr[4];
	int srcport = -1;
	int dstport = -1;
	gboolean uninitialized_var(is_addr_8bit);

	/*
	 * According to the specification, we have to use the last
	 * useable header.  Also, we have to ignore ports that are reserved:
	 * A receiving entity shall ignore (i.e. skip over and commence
	 * processing at the next information element) any information element
	 * where the value of the Information-Element-Data is Reserved or not
	 * supported.
	 */
	while ((iei = sms_udh_iter_get_ie_type(iter)) !=
			SMS_IEI_INVALID) {
		switch (iei) {
		case SMS_IEI_APPLICATION_ADDRESS_8BIT:
			if (sms_udh_iter_get_ie_length(iter) != 2)
				break;

			sms_udh_iter_get_ie_data(iter, addr_hdr);

			if (addr_hdr[0] < 240)
				break;

			if (addr_hdr[1] < 240)
				break;

			dstport = addr_hdr[0];
			srcport = addr_hdr[1];
			is_addr_8bit = TRUE;
			break;

		case SMS_IEI_APPLICATION_ADDRESS_16BIT:
			if (sms_udh_iter_get_ie_length(iter) != 4)
				break;

			sms_udh_iter_get_ie_data(iter, addr_hdr);

			if (((addr_hdr[0] << 8) | addr_hdr[1]) > 49151)
				break;

			if (((addr_hdr[2] << 8) | addr_hdr[3]) > 49151)
				break;

			dstport = (addr_hdr[0] << 8) | addr_hdr[1];
			srcport = (addr_hdr[2] << 8) | addr_hdr[3];
			is_addr_8bit = FALSE;
			break;

		default:
			break;
		}

		sms_udh_iter_next(iter);
	}

	if (dstport == -1 || srcport == -1)
		return FALSE;

	if (dst)
		*dst = dstport;

	if (src)
		*src = srcport;

	if (is_8bit)
		*is_8bit = is_addr_8bit;

	return TRUE;

}

gboolean cbs_decode(const unsigned char *pdu, int len, struct cbs *out)
{
	/* CBS is always a fixed length of 88 bytes */
	if (len != 88)
		return FALSE;

	out->gs = (enum cbs_geo_scope) ((pdu[0] >> 6) & 0x03);
	out->message_code = ((pdu[0] & 0x3f) << 4) | ((pdu[1] >> 4) & 0xf);
	out->update_number = (pdu[1] & 0xf);
	out->message_identifier = (pdu[2] << 8) | pdu[3];
	out->dcs = pdu[4];
	out->max_pages = pdu[5] & 0xf;
	out->page = (pdu[5] >> 4) & 0xf;

	/*
	 * If a mobile receives the code 0000 in either the first field or
	 * the second field then it shall treat the CBS message exactly the
	 * same as a CBS message with page parameter 0001 0001 (i.e. a single
	 * page message).
	 */
	if (out->max_pages == 0 || out->page == 0) {
		out->max_pages = 1;
		out->page = 1;
	}

	memcpy(out->ud, pdu + 6, 82);

	return TRUE;
}

gboolean cbs_encode(const struct cbs *cbs, int *len, unsigned char *pdu)
{
	pdu[0] = (cbs->gs << 6) | ((cbs->message_code >> 4) & 0x3f);
	pdu[1] = ((cbs->message_code & 0xf) << 4) | cbs->update_number;
	pdu[2] = cbs->message_identifier >> 8;
	pdu[3] = cbs->message_identifier & 0xff;
	pdu[4] = cbs->dcs;
	pdu[5] = cbs->max_pages | (cbs->page << 4);

	memcpy(pdu + 6, cbs->ud, 82);

	if (len)
		*len = 88;

	return TRUE;
}

gboolean cbs_extract_app_port(const struct cbs *cbs, int *dst, int *src,
				gboolean *is_8bit)
{
	struct sms_udh_iter iter;

	if (!sms_udh_iter_init_from_cbs(cbs, &iter))
		return FALSE;

	return extract_app_port_common(&iter, dst, src, is_8bit);
}

gboolean iso639_2_from_language(enum cbs_language lang, char *iso639)
{
	switch (lang) {
	case CBS_LANGUAGE_GERMAN:
		iso639[0] = 'd';
		iso639[1] = 'e';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_ENGLISH:
		iso639[0] = 'e';
		iso639[1] = 'n';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_ITALIAN:
		iso639[0] = 'i';
		iso639[1] = 't';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_FRENCH:
		iso639[0] = 'f';
		iso639[1] = 'r';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_SPANISH:
		iso639[0] = 'e';
		iso639[1] = 's';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_DUTCH:
		iso639[0] = 'n';
		iso639[1] = 'l';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_SWEDISH:
		iso639[0] = 's';
		iso639[1] = 'v';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_DANISH:
		iso639[0] = 'd';
		iso639[1] = 'a';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_PORTUGESE:
		iso639[0] = 'p';
		iso639[1] = 't';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_FINNISH:
		iso639[0] = 'f';
		iso639[1] = 'i';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_NORWEGIAN:
		iso639[0] = 'n';
		iso639[1] = 'o';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_GREEK:
		iso639[0] = 'e';
		iso639[1] = 'l';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_TURKISH:
		iso639[0] = 't';
		iso639[1] = 'r';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_HUNGARIAN:
		iso639[0] = 'h';
		iso639[1] = 'u';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_POLISH:
		iso639[0] = 'p';
		iso639[1] = 'l';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_CZECH:
		iso639[0] = 'c';
		iso639[1] = 's';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_HEBREW:
		iso639[0] = 'h';
		iso639[1] = 'e';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_ARABIC:
		iso639[0] = 'a';
		iso639[1] = 'r';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_RUSSIAN:
		iso639[0] = 'r';
		iso639[1] = 'u';
		iso639[2] = '\0';
		return TRUE;
	case CBS_LANGUAGE_ICELANDIC:
		iso639[0] = 'i';
		iso639[1] = 's';
		iso639[2] = '\0';
		return TRUE;
	default:
		iso639[0] = '\0';
		break;
	}

	return FALSE;
}

gboolean cbs_dcs_decode(guint8 dcs, gboolean *udhi, enum sms_class *cls,
			enum sms_charset *charset, gboolean *compressed,
			enum cbs_language *language, gboolean *iso639)
{
	guint8 upper = (dcs & 0xf0) >> 4;
	guint8 lower = dcs & 0xf;
	enum sms_charset ch;
	enum sms_class cl;
	enum cbs_language lang = CBS_LANGUAGE_UNSPECIFIED;
	gboolean iso = FALSE;
	gboolean comp = FALSE;
	gboolean udh = FALSE;

	if (upper == 0x3 || upper == 0x8 || (upper >= 0xA && upper <= 0xE))
		return FALSE;

	switch (upper) {
	case 0:
		ch = SMS_CHARSET_7BIT;
		cl = SMS_CLASS_UNSPECIFIED;
		lang = (enum cbs_language) lower;
		break;
	case 1:
		if (lower > 1)
			return FALSE;

		if (lower == 0)
			ch = SMS_CHARSET_7BIT;
		else
			ch = SMS_CHARSET_UCS2;

		cl = SMS_CLASS_UNSPECIFIED;
		iso = TRUE;

		break;
	case 2:
		if (lower > 4)
			return FALSE;

		ch = SMS_CHARSET_7BIT;
		cl = SMS_CLASS_UNSPECIFIED;
		lang = (enum cbs_language) dcs;
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		comp = (dcs & 0x20) ? TRUE : FALSE;

		if (dcs & 0x10)
			cl = (enum sms_class) (dcs & 0x03);
		else
			cl = SMS_CLASS_UNSPECIFIED;

		if (((dcs & 0x0c) >> 2) < 3)
			ch = (enum sms_charset) ((dcs & 0x0c) >> 2);
		else
			return FALSE;

		break;
	case 9:
		udh = TRUE;
		cl = (enum sms_class) (dcs & 0x03);
		if (((dcs & 0x0c) >> 2) < 3)
			ch = (enum sms_charset) ((dcs & 0x0c) >> 2);
		else
			return FALSE;

		break;
	case 15:
		if (lower & 0x8)
			return FALSE;

		if (lower & 0x4)
			ch = SMS_CHARSET_8BIT;
		else
			ch = SMS_CHARSET_7BIT;

		if (lower & 0x3)
			cl = (enum sms_class) (lower & 0x3);
		else
			cl = SMS_CLASS_UNSPECIFIED;

		break;
	default:
		return FALSE;
	};

	if (udhi)
		*udhi = udh;

	if (cls)
		*cls = cl;

	if (charset)
		*charset = ch;

	if (compressed)
		*compressed = comp;

	if (language)
		*language = lang;

	if (iso639)
		*iso639 = iso;

	return TRUE;
}

gboolean sms_udh_iter_init_from_cbs(const struct cbs *cbs,
					struct sms_udh_iter *iter)
{
	gboolean udhi = FALSE;
	const guint8 *hdr;
	guint8 max_ud_len;

	cbs_dcs_decode(cbs->dcs, &udhi, NULL, NULL, NULL, NULL, NULL);

	if (!udhi)
		return FALSE;

	hdr = cbs->ud;
	max_ud_len = 82;

	/* Must have at least one information-element if udhi is true */
	if (hdr[0] < 2)
		return FALSE;

	if (hdr[0] >= max_ud_len)
		return FALSE;

	if (!verify_udh(hdr, max_ud_len))
		return FALSE;

	iter->data = hdr;
	iter->offset = 1;

	return TRUE;
}

guint8 sms_udh_iter_get_udh_length(struct sms_udh_iter *iter)
{
	return iter->data[0];
}

char *cbs_decode_text(GSList *cbs_list, char *iso639_lang)
{
	GSList *l;
	const struct cbs *cbs;
	enum sms_charset uninitialized_var(charset);
	enum cbs_language lang;
	gboolean uninitialized_var(iso639);
	int bufsize = 0;
	unsigned char *buf;
	char *utf8;

	if (cbs_list == NULL)
		return NULL;

	/*
	 * CBS can only come from the network, so we're much less lenient
	 * on what we support.  Namely we require the same charset to be
	 * used across all pages.
	 */
	for (l = cbs_list; l; l = l->next) {
		enum sms_charset curch;
		gboolean curiso;

		cbs = l->data;

		if (!cbs_dcs_decode(cbs->dcs, NULL, NULL,
					&curch, NULL, &lang, &curiso))
			return NULL;

		if (l == cbs_list) {
			iso639 = curiso;
			charset = curch;
		}

		if (curch != charset)
			return NULL;

		if (curiso != iso639)
			return NULL;

		if (curch == SMS_CHARSET_8BIT)
			return NULL;

		if (curch == SMS_CHARSET_7BIT) {
			bufsize += CBS_MAX_GSM_CHARS;

			if (iso639)
				bufsize -= 3;
		} else {
			bufsize += 82;

			if (iso639)
				bufsize -= 2;
		}
	}

	if (lang) {
		cbs = cbs_list->data;

		if (iso639) {
			struct sms_udh_iter iter;
			int taken = 0;

			if (sms_udh_iter_init_from_cbs(cbs, &iter))
				taken = sms_udh_iter_get_udh_length(&iter) + 1;

			unpack_7bit_own_buf(cbs->ud + taken, 82 - taken,
						taken, FALSE, 2,
						NULL, 0,
						(unsigned char *)iso639_lang);
			iso639_lang[2] = '\0';
		} else {
			iso639_2_from_language(lang, iso639_lang);
		}
	}

	buf = g_new(unsigned char, bufsize);
	bufsize = 0;

	for (l = cbs_list; l; l = l->next) {
		const guint8 *ud;
		struct sms_udh_iter iter;
		int taken = 0;

		cbs = l->data;
		ud = cbs->ud;

		if (sms_udh_iter_init_from_cbs(cbs, &iter))
			taken = sms_udh_iter_get_udh_length(&iter) + 1;

		if (charset == SMS_CHARSET_7BIT) {
			unsigned char unpacked[CBS_MAX_GSM_CHARS];
			long written;
			int max_chars;
			int i;

			max_chars =
				sms_text_capacity_gsm(CBS_MAX_GSM_CHARS, taken);

			unpack_7bit_own_buf(ud + taken, 82 - taken,
						taken, FALSE, max_chars,
						&written, 0, unpacked);

			i = iso639 ? 3 : 0;

			/*
			 * CR is a padding character, which means we can
			 * safely discard everything afterwards
			 */
			for (; i < written; i++, bufsize++) {
				if (unpacked[i] == '\r')
					break;

				buf[bufsize] = unpacked[i];
			}

			/*
			 * It isn't clear whether extension sequences
			 * (2 septets) must be wholly present in the page
			 * and not broken over multiple pages.  The behavior
			 * is probably the same as SMS, but we don't make
			 * the check here since the specification isn't clear
			 */
		} else {
			int num_ucs2_chars = (82 - taken) >> 1;
			int i = taken;
			int max_offset = taken + num_ucs2_chars * 2;

			/*
			 * It is completely unclear how UCS2 chars are handled
			 * especially across pages or when the UDH is present.
			 * For now do the best we can.
			 */
			if (iso639) {
				i += 2;
				num_ucs2_chars -= 1;
			}

			while (i < max_offset) {
				if (ud[i] == 0x00 && ud[i+1] == '\r')
					break;

				buf[bufsize] = ud[i];
				buf[bufsize + 1] = ud[i+1];

				bufsize += 2;
				i += 2;
			}
		}
	}

	if (charset == SMS_CHARSET_7BIT)
		utf8 = convert_gsm_to_utf8(buf, bufsize, NULL, NULL, 0);
	else
		utf8 = g_convert((char *) buf, bufsize, "UTF-8//TRANSLIT",
					"UCS-2BE", NULL, NULL, NULL);

	g_free(buf);
	return utf8;
}

static inline gboolean cbs_is_update_newer(unsigned int n, unsigned int o)
{
	unsigned int old_update = o & 0xf;
	unsigned int new_update = n & 0xf;

	if (new_update == old_update)
		return FALSE;

	/*
	 * Any Update Number eight or less higher (modulo 16) than the last
	 * received Update Number will be considered more recent, and shall be
	 * treated as a new CBS message, provided the mobile has not been
	 * switched off.
	 */
	if (new_update <= ((old_update + 8) % 16))
		return TRUE;

	return FALSE;
}

struct cbs_assembly *cbs_assembly_new()
{
	return g_new0(struct cbs_assembly, 1);
}

void cbs_assembly_free(struct cbs_assembly *assembly)
{
	GSList *l;

	for (l = assembly->assembly_list; l; l = l->next) {
		struct cbs_assembly_node *node = l->data;

		g_slist_foreach(node->pages, (GFunc) g_free, 0);
		g_slist_free(node->pages);
		g_free(node);
	}

	g_slist_free(assembly->assembly_list);
	g_slist_free(assembly->recv_plmn);
	g_slist_free(assembly->recv_loc);
	g_slist_free(assembly->recv_cell);

	g_free(assembly);
}

static gint cbs_compare_node_by_gs(gconstpointer a, gconstpointer b)
{
	const struct cbs_assembly_node *node = a;
	unsigned int gs = GPOINTER_TO_UINT(b);

	if (((node->serial >> 14) & 0x3) == gs)
		return 0;

	return 1;
}

static gint cbs_compare_node_by_update(gconstpointer a, gconstpointer b)
{
	const struct cbs_assembly_node *node = a;
	unsigned int serial = GPOINTER_TO_UINT(b);

	if ((serial & (~0xf)) != (node->serial & (~0xf)))
		return 1;

	if (cbs_is_update_newer(node->serial, serial))
		return 1;

	return 0;
}

static gint cbs_compare_recv_by_serial(gconstpointer a, gconstpointer b)
{
	unsigned int old_serial = GPOINTER_TO_UINT(a);
	unsigned int new_serial = GPOINTER_TO_UINT(b);

	if ((old_serial & (~0xf)) == (new_serial & (~0xf)))
		return 0;

	return 1;
}

static void cbs_assembly_expire(struct cbs_assembly *assembly,
				GCompareFunc func, gconstpointer *userdata)
{
	GSList *l;
	GSList *prev;
	GSList *tmp;

	/*
	 * Take care of the case where several updates are being
	 * reassembled at the same time. If the newer one is assembled
	 * first, then the subsequent old update is discarded, make
	 * sure that we're also discarding the assembly node for the
	 * partially assembled ones
	 */
	prev = NULL;
	l = assembly->assembly_list;

	while (l) {
		struct cbs_assembly_node *node = l->data;

		if (func(node, userdata) != 0) {
			prev = l;
			l = l->next;
			continue;
		}

		if (prev)
			prev->next = l->next;
		else
			assembly->assembly_list = l->next;

		g_slist_foreach(node->pages, (GFunc) g_free, NULL);
		g_slist_free(node->pages);
		g_free(node->pages);
		tmp = l;
		l = l->next;
		g_slist_free_1(tmp);
	}
}

void cbs_assembly_location_changed(struct cbs_assembly *assembly, gboolean plmn,
					gboolean lac, gboolean ci)
{
	/*
	 * Location Area wide (in GSM) (which means that a CBS message with the
	 * same Message Code and Update Number may or may not be "new" in the
	 * next cell according to whether the next cell is in the same Location
	 * Area as the current cell), or
	 *
	 * Service Area Wide (in UMTS) (which means that a CBS message with the
	 * same Message Code and Update Number may or may not be "new" in the
	 * next cell according to whether the next cell is in the same Service
	 * Area as the current cell)
	 *
	 * NOTE 4: According to 3GPP TS 23.003 [2] a Service Area consists of
	 * one cell only.
	 */

	if (plmn) {
		lac = TRUE;
		g_slist_free(assembly->recv_plmn);
		assembly->recv_plmn = NULL;

		cbs_assembly_expire(assembly, cbs_compare_node_by_gs,
				GUINT_TO_POINTER(CBS_GEO_SCOPE_PLMN));
	}

	if (lac) {
		/* If LAC changed, then cell id has changed */
		ci = TRUE;
		g_slist_free(assembly->recv_loc);
		assembly->recv_loc = NULL;

		cbs_assembly_expire(assembly, cbs_compare_node_by_gs,
				GUINT_TO_POINTER(CBS_GEO_SCOPE_SERVICE_AREA));
	}

	if (ci) {
		g_slist_free(assembly->recv_cell);
		assembly->recv_cell = NULL;
		cbs_assembly_expire(assembly, cbs_compare_node_by_gs,
				GUINT_TO_POINTER(CBS_GEO_SCOPE_CELL_IMMEDIATE));
		cbs_assembly_expire(assembly, cbs_compare_node_by_gs,
				GUINT_TO_POINTER(CBS_GEO_SCOPE_CELL_NORMAL));
	}
}

GSList *cbs_assembly_add_page(struct cbs_assembly *assembly,
				const struct cbs *cbs)
{
	struct cbs *newcbs;
	struct cbs_assembly_node *node;
	GSList *completed;
	unsigned int new_serial;
	GSList **recv;
	GSList *l;
	GSList *prev;
	int position;

	new_serial = cbs->gs << 14;
	new_serial |= cbs->message_code << 4;
	new_serial |= cbs->update_number;
	new_serial |= cbs->message_identifier << 16;

	if (cbs->gs == CBS_GEO_SCOPE_PLMN)
		recv = &assembly->recv_plmn;
	else if (cbs->gs == CBS_GEO_SCOPE_SERVICE_AREA)
		recv = &assembly->recv_loc;
	else
		recv = &assembly->recv_cell;

	/* Have we seen this message before? */
	l = g_slist_find_custom(*recv, GUINT_TO_POINTER(new_serial),
				cbs_compare_recv_by_serial);

	/* If we have, is the message newer? */
	if (l && !cbs_is_update_newer(new_serial, GPOINTER_TO_UINT(l->data)))
		return NULL;

	/* Easy case first, page 1 of 1 */
	if (cbs->max_pages == 1 && cbs->page == 1) {
		if (l)
			l->data = GUINT_TO_POINTER(new_serial);
		else
			*recv = g_slist_prepend(*recv,
						GUINT_TO_POINTER(new_serial));

		newcbs = g_new(struct cbs, 1);
		memcpy(newcbs, cbs, sizeof(struct cbs));
		completed = g_slist_append(NULL, newcbs);

		return completed;
	}

	prev = NULL;
	position = 0;

	for (l = assembly->assembly_list; l; prev = l, l = l->next) {
		int j;
		node = l->data;

		if (new_serial != node->serial)
			continue;

		if (node->bitmap & (1 << cbs->page))
			return NULL;

		for (j = 1; j < cbs->page; j++)
			if (node->bitmap & (1 << j))
				position += 1;

		goto out;
	}

	node = g_new0(struct cbs_assembly_node, 1);
	node->serial = new_serial;

	assembly->assembly_list = g_slist_prepend(assembly->assembly_list,
							node);

	prev = NULL;
	l = assembly->assembly_list;
	position = 0;

out:
	newcbs = g_new(struct cbs, 1);
	memcpy(newcbs, cbs, sizeof(struct cbs));
	node->pages = g_slist_insert(node->pages, newcbs, position);
	node->bitmap |= 1 << cbs->page;

	if (g_slist_length(node->pages) < cbs->max_pages)
		return NULL;

	completed = node->pages;

	if (prev)
		prev->next = l->next;
	else
		assembly->assembly_list = l->next;

	g_free(node);
	g_slist_free_1(l);

	cbs_assembly_expire(assembly, cbs_compare_node_by_update,
				GUINT_TO_POINTER(new_serial));
	*recv = g_slist_prepend(*recv, GUINT_TO_POINTER(new_serial));

	return completed;
}

static inline int skip_to_next_field(const char *str, int pos, int len)
{
	if (pos < len && str[pos] == ',')
		pos += 1;

	while (pos < len && str[pos] == ' ')
		pos += 1;

	return pos;
}

static gboolean next_range(const char *str, int *offset, gint *min, gint *max)
{
	int pos;
	int end;
	int len;
	int low = 0;
	int high = 0;

	len = strlen(str);

	pos = *offset;

	while (pos < len && str[pos] == ' ')
		pos += 1;

	end = pos;

	while (str[end] >= '0' && str[end] <= '9') {
		low = low * 10 + (int)(str[end] - '0');
		end += 1;
	}

	if (pos == end)
		return FALSE;

	if (str[end] != '-') {
		high = low;
		goto out;
	}

	pos = end = end + 1;

	while (str[end] >= '0' && str[end] <= '9') {
		high = high * 10 + (int)(str[end] - '0');
		end += 1;
	}

	if (pos == end)
		return FALSE;

out:
	*offset = skip_to_next_field(str, end, len);

	if (min)
		*min = low;

	if (max)
		*max = high;

	return TRUE;
}

GSList *cbs_optimize_ranges(GSList *ranges)
{
	struct cbs_topic_range *range;
	unsigned char bitmap[125];
	GSList *l;
	unsigned short i;
	GSList *ret = NULL;

	memset(bitmap, 0, sizeof(bitmap));

	for (l = ranges; l; l = l->next) {
		range = l->data;

		for (i = range->min; i <= range->max; i++) {
			int byte_offset = i / 8;
			int bit = i % 8;

			bitmap[byte_offset] |= 1 << bit;
		}
	}

	range = NULL;

	for (i = 0; i <= 999; i++) {
		int byte_offset = i / 8;
		int bit = i % 8;

		if (is_bit_set(bitmap[byte_offset], bit) == FALSE) {
			if (range) {
				ret = g_slist_prepend(ret, range);
				range = NULL;
			}

			continue;
		}

		if (range) {
			range->max = i;
			continue;
		}

		range = g_new0(struct cbs_topic_range, 1);
		range->min = i;
		range->max = i;
	}

	if (range != NULL)
		ret = g_slist_prepend(ret, range);

	ret = g_slist_reverse(ret);

	return ret;
}

GSList *cbs_extract_topic_ranges(const char *ranges)
{
	int min;
	int max;
	int offset = 0;
	GSList *ret = NULL;
	GSList *tmp;

	while (next_range(ranges, &offset, &min, &max) == TRUE) {
		if (min < 0 || min > 999)
			return NULL;

		if (max < 0 || max > 999)
			return NULL;

		if (max < min)
			return NULL;
	}

	if (ranges[offset] != '\0')
		return NULL;

	offset = 0;

	while (next_range(ranges, &offset, &min, &max) == TRUE) {
		struct cbs_topic_range *range = g_new0(struct cbs_topic_range, 1);

		range->min = min;
		range->max = max;

		ret = g_slist_prepend(ret, range);
	}

	tmp = cbs_optimize_ranges(ret);
	g_slist_foreach(ret, (GFunc) g_free, NULL);
	g_slist_free(ret);

	return tmp;
}

static inline int element_length(unsigned short element)
{
	if (element <= 9)
		return 1;

	if (element <= 99)
		return 2;

	if (element <= 999)
		return 3;

	if (element <= 9999)
		return 4;

	return 5;
}

static inline int range_length(struct cbs_topic_range *range)
{
	if (range->min == range->max)
		return element_length(range->min);

	return element_length(range->min) + element_length(range->max) + 1;
}

char *cbs_topic_ranges_to_string(GSList *ranges)
{
	int len = 0;
	int nelem = 0;
	struct cbs_topic_range *range;
	GSList *l;
	char *ret;

	if (ranges == NULL)
		return g_new0(char, 1);

	for (l = ranges; l; l = l->next) {
		range = l->data;

		len += range_length(range);
		nelem += 1;
	}

	/* Space for ranges, commas and terminator null */
	ret = g_new(char, len + nelem);

	len = 0;

	for (l = ranges; l; l = l->next) {
		range = l->data;

		if (range->min != range->max)
			len += sprintf(ret + len, "%hu-%hu",
					range->min, range->max);
		else
			len += sprintf(ret + len, "%hu", range->min);

		if (l->next != NULL)
			ret[len++] = ',';
	}

	return ret;
}

static gint cbs_topic_compare(gconstpointer a, gconstpointer b)
{
	const struct cbs_topic_range *range = a;
	unsigned short topic = GPOINTER_TO_UINT(b);

	if (topic >= range->min && topic <= range->max)
		return 0;

	return 1;
}

gboolean cbs_topic_in_range(unsigned int topic, GSList *ranges)
{
	if (!ranges)
		return FALSE;

	return g_slist_find_custom(ranges, GUINT_TO_POINTER(topic),
					cbs_topic_compare) != NULL;
}

char *ussd_decode(int dcs, int len, const unsigned char *data)
{
	gboolean udhi;
	enum sms_charset charset;
	gboolean compressed;
	gboolean iso639;
	char *utf8;

	if (!cbs_dcs_decode(dcs, &udhi, NULL, &charset,
				&compressed, NULL, &iso639))
		return NULL;

	if (udhi || compressed || iso639)
		return NULL;

	switch (charset) {
	case SMS_CHARSET_7BIT:
	{
		long written;
		unsigned char *unpacked = unpack_7bit(data, len, 0, TRUE, 0,
							&written, 0);
		if (unpacked == NULL)
			return NULL;

		utf8 = convert_gsm_to_utf8(unpacked, written, NULL, NULL, 0);
		g_free(unpacked);

		break;
	}
	case SMS_CHARSET_8BIT:
		utf8 = convert_gsm_to_utf8(data, len, NULL, NULL, 0);
		break;
	case SMS_CHARSET_UCS2:
		utf8 = g_convert((const gchar *) data, len,
					"UTF-8//TRANSLIT", "UCS-2BE",
					NULL, NULL, NULL);
		break;
	default:
		utf8 = NULL;
	}

	return utf8;
}

gboolean ussd_encode(const char *str, long *items_written, unsigned char *pdu)
{
	unsigned char *converted = NULL;
	long written;
	long num_packed;

	if (!pdu)
		return FALSE;

	converted = convert_utf8_to_gsm(str, -1, NULL, &written, 0);
	if (!converted || written > 182) {
		g_free(converted);
		return FALSE;
	}

	pack_7bit_own_buf(converted, written, 0, TRUE, &num_packed, 0, pdu);
	g_free(converted);

	if (num_packed < 1)
		return FALSE;

	if (items_written)
		*items_written = num_packed;

	return TRUE;
}
