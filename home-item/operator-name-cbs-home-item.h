/*
 *  operator-name-cbs-home-item (operator name item)
 *  Copyright (C) 2011 Nicolai Hess/Jonathan Wilson
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  
 * Based on custom-operator-name-widget by faheem pervez (qwerty12) 
 */

#ifndef _OPERATOR_NAME_CBS_SP_H_
#define _OPERATOR_NAME_CBS_SP_H_

#include <libhildondesktop/libhildondesktop.h>

G_BEGIN_DECLS

#define TYPE_OPERATOR_NAME_CBS_HOME_ITEM (operator_name_cbs_home_item_get_type())
#define OPERATOR_NAME_CBS_HOME_ITEM(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_OPERATOR_NAME_CBS_HOME_ITEM, OperatorNameCBSHomeItem))
#define OPERATOR_NAME_CBS_HOME_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), TYPE_OPERATOR_NAME_CBS_HOME_ITEM, OperatorNameCBSHomeItemClass))
#define IS_OPERATOR_NAME_CBS_HOME_ITEM(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), TYPE_OPERATOR_NAME_CBS_HOME_ITEM))
#define IS_OPERATOR_NAME_CBS_HOME_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), TYPE_OPERATOR_NAME_CBS_HOME_ITEM))
#define OPERATOR_NAME_CBS_HOME_ITEM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), TYPE_OPERATOR_NAME_CBS_HOME_ITEM, OperatorNameCBSHomeItemClass))

typedef struct _OperatorNameCBSHomeItem OperatorNameCBSHomeItem;
typedef struct _OperatorNameCBSHomeItemClass OperatorNameCBSHomeItemClass;
typedef struct _OperatorNameCBSHomeItemPrivate OperatorNameCBSHomeItemPrivate;

struct _OperatorNameCBSHomeItem
{
  HDHomePluginItem hitem;
  OperatorNameCBSHomeItemPrivate* priv;
};

struct _OperatorNameCBSHomeItemClass
{
  HDHomePluginItemClass parent;
};

GType operator_name_cbs_home_item_get_type(void);

G_END_DECLS

#endif
