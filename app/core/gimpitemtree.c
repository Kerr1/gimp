/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995-1997 Spencer Kimball and Peter Mattis
 *
 * gimpitemtree.c
 * Copyright (C) 2010 Michael Natterer <mitch@gimp.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <gegl.h>

#include "core-types.h"

#include "gimpimage.h"
#include "gimpimage-undo-push.h"
#include "gimpitem.h"
#include "gimpitemstack.h"
#include "gimpitemtree.h"


enum
{
  PROP_0,
  PROP_IMAGE,
  PROP_CONTAINER_TYPE,
  PROP_ITEM_TYPE,
  PROP_ACTIVE_ITEM
};


typedef struct _GimpItemTreePrivate GimpItemTreePrivate;

struct _GimpItemTreePrivate
{
  GimpImage  *image;

  GType       container_type;
  GType       item_type;

  GimpItem   *active_item;

  GHashTable *name_hash;
};

#define GIMP_ITEM_TREE_GET_PRIVATE(object) \
        G_TYPE_INSTANCE_GET_PRIVATE (object, \
                                     GIMP_TYPE_ITEM_TREE, \
                                     GimpItemTreePrivate)


/*  local function prototypes  */

static GObject * gimp_item_tree_constructor   (GType                  type,
                                               guint                  n_params,
                                               GObjectConstructParam *params);
static void      gimp_item_tree_finalize      (GObject               *object);
static void      gimp_item_tree_set_property  (GObject               *object,
                                               guint                  property_id,
                                               const GValue          *value,
                                               GParamSpec            *pspec);
static void      gimp_item_tree_get_property  (GObject               *object,
                                               guint                  property_id,
                                               GValue                *value,
                                               GParamSpec            *pspec);

static gint64    gimp_item_tree_get_memsize   (GimpObject            *object,
                                               gint64                *gui_size);

static void      gimp_item_tree_uniquefy_name (GimpItemTree          *tree,
                                               GimpItem              *item,
                                               const gchar           *new_name);


G_DEFINE_TYPE (GimpItemTree, gimp_item_tree, GIMP_TYPE_OBJECT)

#define parent_class gimp_item_tree_parent_class


static void
gimp_item_tree_class_init (GimpItemTreeClass *klass)
{
  GObjectClass    *object_class      = G_OBJECT_CLASS (klass);
  GimpObjectClass *gimp_object_class = GIMP_OBJECT_CLASS (klass);

  object_class->constructor      = gimp_item_tree_constructor;
  object_class->finalize         = gimp_item_tree_finalize;
  object_class->set_property     = gimp_item_tree_set_property;
  object_class->get_property     = gimp_item_tree_get_property;

  gimp_object_class->get_memsize = gimp_item_tree_get_memsize;

  g_object_class_install_property (object_class, PROP_IMAGE,
                                   g_param_spec_object ("image",
                                                        NULL, NULL,
                                                        GIMP_TYPE_IMAGE,
                                                        GIMP_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class, PROP_CONTAINER_TYPE,
                                   g_param_spec_gtype ("container-type",
                                                       NULL, NULL,
                                                       GIMP_TYPE_ITEM_STACK,
                                                       GIMP_PARAM_READWRITE |
                                                       G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class, PROP_ITEM_TYPE,
                                   g_param_spec_gtype ("item-type",
                                                       NULL, NULL,
                                                       GIMP_TYPE_ITEM,
                                                       GIMP_PARAM_READWRITE |
                                                       G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class, PROP_ACTIVE_ITEM,
                                   g_param_spec_object ("active-item",
                                                        NULL, NULL,
                                                        GIMP_TYPE_ITEM,
                                                        GIMP_PARAM_READWRITE));

  g_type_class_add_private (klass, sizeof (GimpItemTreePrivate));
}

static void
gimp_item_tree_init (GimpItemTree *tree)
{
  GimpItemTreePrivate *private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  private->name_hash = g_hash_table_new (g_str_hash, g_str_equal);
}

static GObject *
gimp_item_tree_constructor (GType                  type,
                            guint                  n_params,
                            GObjectConstructParam *params)
{
  GObject             *object;
  GimpItemTree        *tree;
  GimpItemTreePrivate *private;

  object = G_OBJECT_CLASS (parent_class)->constructor (type, n_params, params);

  tree    = GIMP_ITEM_TREE (object);
  private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  g_assert (GIMP_IS_IMAGE (private->image));
  g_assert (g_type_is_a (private->container_type, GIMP_TYPE_ITEM_STACK));
  g_assert (g_type_is_a (private->item_type,      GIMP_TYPE_ITEM));
  g_assert (private->item_type != GIMP_TYPE_ITEM);

  tree->container = g_object_new (private->container_type,
                                  "name",          g_type_name (private->item_type),
                                  "children-type", private->item_type,
                                  "policy",        GIMP_CONTAINER_POLICY_STRONG,
                                  NULL);

  return object;
}

static void
gimp_item_tree_finalize (GObject *object)
{
  GimpItemTree        *tree    = GIMP_ITEM_TREE (object);
  GimpItemTreePrivate *private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  if (private->name_hash)
    {
      g_hash_table_unref (private->name_hash);
      private->name_hash = NULL;
    }

  if (tree->container)
    {
      g_object_unref (tree->container);
      tree->container = NULL;
    }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gimp_item_tree_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GimpItemTreePrivate *private = GIMP_ITEM_TREE_GET_PRIVATE (object);

  switch (property_id)
    {
    case PROP_IMAGE:
      private->image = g_value_get_object (value); /* don't ref */
      break;
    case PROP_CONTAINER_TYPE:
      private->container_type = g_value_get_gtype (value);
      break;
    case PROP_ITEM_TYPE:
      private->item_type = g_value_get_gtype (value);
      break;
    case PROP_ACTIVE_ITEM:
      private->active_item = g_value_get_object (value); /* don't ref */
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gimp_item_tree_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  GimpItemTreePrivate *private = GIMP_ITEM_TREE_GET_PRIVATE (object);

  switch (property_id)
    {
    case PROP_IMAGE:
      g_value_set_object (value, private->image);
      break;
    case PROP_CONTAINER_TYPE:
      g_value_set_gtype (value, private->container_type);
      break;
    case PROP_ITEM_TYPE:
      g_value_set_gtype (value, private->item_type);
      break;
    case PROP_ACTIVE_ITEM:
      g_value_set_object (value, private->active_item);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static gint64
gimp_item_tree_get_memsize (GimpObject *object,
                            gint64     *gui_size)
{
  GimpItemTree *tree    = GIMP_ITEM_TREE (object);
  gint64        memsize = 0;

  memsize += gimp_object_get_memsize (GIMP_OBJECT (tree->container), gui_size);

  return memsize + GIMP_OBJECT_CLASS (parent_class)->get_memsize (object,
                                                                  gui_size);
}


/*  public functions  */

GimpItemTree *
gimp_item_tree_new (GimpImage *image,
                    GType      container_type,
                    GType      item_type)
{
  g_return_val_if_fail (GIMP_IS_IMAGE (image), NULL);
  g_return_val_if_fail (g_type_is_a (container_type, GIMP_TYPE_ITEM_STACK), NULL);
  g_return_val_if_fail (g_type_is_a (item_type, GIMP_TYPE_ITEM), NULL);

  return g_object_new (GIMP_TYPE_ITEM_TREE,
                       "image",          image,
                       "container-type", container_type,
                       "item-type",      item_type,
                       NULL);
}

GimpItem *
gimp_item_tree_get_active_item (GimpItemTree *tree)
{
  g_return_val_if_fail (GIMP_IS_ITEM_TREE (tree), NULL);

  return GIMP_ITEM_TREE_GET_PRIVATE (tree)->active_item;
}

void
gimp_item_tree_set_active_item (GimpItemTree *tree,
                                GimpItem     *item)
{
  GimpItemTreePrivate *private;

  g_return_if_fail (GIMP_IS_ITEM_TREE (tree));
  g_return_if_fail (item == NULL || GIMP_IS_ITEM (item));

  private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  g_return_if_fail (item == NULL ||
                    (gimp_item_is_attached (item) &&
                     gimp_item_get_image (item) == private->image));

  if (item != private->active_item)
    {
      private->active_item = item;

      g_object_notify (G_OBJECT (tree), "active-item");
    }
}

GimpItem *
gimp_item_tree_get_item_by_name (GimpItemTree *tree,
                                 const gchar  *name)
{
  g_return_val_if_fail (GIMP_IS_ITEM_TREE (tree), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  return g_hash_table_lookup (GIMP_ITEM_TREE_GET_PRIVATE (tree)->name_hash,
                              name);
}

GimpItem *
gimp_item_tree_get_insert_pos (GimpItemTree *tree,
                               GimpItem     *parent,
                               gint         *position)
{
  GimpItemTreePrivate *private;
  GimpContainer       *container;

  g_return_val_if_fail (GIMP_IS_ITEM_TREE (tree), NULL);

  private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  /*  if we want to insert in the active item's parent container  */
  if (parent == GIMP_IMAGE_ACTIVE_PARENT)
    {
      if (private->active_item)
        {
          /*  if the active item is a branch, add to the top of that
           *  branch; add to the active item's parent container
           *  otherwise
           */
          if (gimp_viewable_get_children (GIMP_VIEWABLE (private->active_item)))
            {
              parent    = private->active_item;
              *position = 0;
            }
          else
            {
              parent = gimp_item_get_parent (private->active_item);
            }
        }
      else
        {
          /*  use the toplevel container if there is no active item  */
          parent = NULL;
        }
    }

  if (parent)
    container = gimp_viewable_get_children (GIMP_VIEWABLE (parent));
  else
    container = tree->container;

  /*  if we want to add on top of the active item  */
  if (*position == -1)
    {
      if (private->active_item)
        *position = gimp_container_get_child_index (container,
                                                    GIMP_OBJECT (private->active_item));

      /*  if the active item is not in the specified parent container,
       *  fall back to index 0
       */
      if (*position == -1)
        *position = 0;
    }

  /*  don't add at a non-existing index  */
  *position = CLAMP (*position, 0, gimp_container_get_n_children (container));

  return parent;
}

void
gimp_item_tree_add_item (GimpItemTree *tree,
                         GimpItem     *item,
                         GimpItem     *parent,
                         gint          position)
{
  GimpItemTreePrivate *private;
  GimpContainer       *container;

  g_return_if_fail (GIMP_IS_ITEM_TREE (tree));

  private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  g_return_if_fail (GIMP_IS_ITEM (item));
  g_return_if_fail (! gimp_item_is_attached (item));
  g_return_if_fail (gimp_item_get_image (item) == private->image);

  gimp_item_tree_uniquefy_name (tree, item, NULL);

  if (parent)
    container = gimp_viewable_get_children (GIMP_VIEWABLE (parent));
  else
    container = tree->container;

  if (parent)
    gimp_viewable_set_parent (GIMP_VIEWABLE (item),
                              GIMP_VIEWABLE (parent));

  gimp_container_insert (container, GIMP_OBJECT (item), position);
}

GimpItem *
gimp_item_tree_remove_item (GimpItemTree *tree,
                            GimpItem     *item,
                            GimpItem     *new_active)
{
  GimpItemTreePrivate *private;
  GimpItem            *parent;
  GimpContainer       *container;
  gint                 index;

  g_return_val_if_fail (GIMP_IS_ITEM_TREE (tree), NULL);

  private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  g_return_val_if_fail (GIMP_IS_ITEM (item), NULL);
  g_return_val_if_fail (gimp_item_is_attached (item), NULL);
  g_return_val_if_fail (gimp_item_get_image (item) == private->image, NULL);

  parent    = gimp_item_get_parent (item);
  container = gimp_item_get_container (item);
  index     = gimp_item_get_index (item);

  g_object_ref (item);

  gimp_container_remove (container, GIMP_OBJECT (item));

  if (parent)
    gimp_viewable_set_parent (GIMP_VIEWABLE (item), NULL);

  gimp_item_removed (item);

  if (! new_active)
    {
      gint n_children = gimp_container_get_n_children (container);

      if (n_children > 0)
        {
          index = CLAMP (index, 0, n_children - 1);

          new_active =
            GIMP_ITEM (gimp_container_get_child_by_index (container, index));
        }
      else if (parent)
        {
          new_active = parent;
        }
    }

  g_object_unref (item);

  return new_active;
}

gboolean
gimp_item_tree_reorder_item (GimpItemTree *tree,
                             GimpItem     *item,
                             GimpItem     *new_parent,
                             gint          new_index,
                             gboolean      push_undo,
                             const gchar  *undo_desc)
{
  GimpItemTreePrivate *private;
  GimpContainer       *container;
  GimpContainer       *new_container;
  gint                 n_items;

  g_return_val_if_fail (GIMP_IS_ITEM_TREE (tree), FALSE);

  private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  g_return_val_if_fail (G_TYPE_CHECK_INSTANCE_TYPE (item, private->item_type),
                        FALSE);
  g_return_val_if_fail (gimp_item_is_attached (item), FALSE);
  g_return_val_if_fail (gimp_item_get_image (item) == private->image, FALSE);
  g_return_val_if_fail (new_parent == NULL ||
                        G_TYPE_CHECK_INSTANCE_TYPE (new_parent,
                                                    private->item_type),
                        FALSE);
  g_return_val_if_fail (new_parent == NULL ||
                        gimp_item_is_attached (new_parent), FALSE);
  g_return_val_if_fail (new_parent == NULL ||
                        gimp_item_get_image (new_parent) == private->image,
                        FALSE);
  g_return_val_if_fail (new_parent == NULL ||
                        gimp_viewable_get_children (GIMP_VIEWABLE (new_parent)),
                        FALSE);

  container = gimp_item_get_container (item);

  if (new_parent)
    new_container = gimp_viewable_get_children (GIMP_VIEWABLE (new_parent));
  else
    new_container = tree->container;

  n_items = gimp_container_get_n_children (new_container);

  if (new_container == container)
    n_items--;

  new_index = CLAMP (new_index, 0, n_items);

  if (new_container != container ||
      new_index     != gimp_item_get_index (item))
    {
      if (push_undo)
        gimp_image_undo_push_item_reorder (private->image, undo_desc, item);

      if (new_container != container)
        {
          g_object_ref (item);

          gimp_container_remove (container, GIMP_OBJECT (item));

          gimp_viewable_set_parent (GIMP_VIEWABLE (item),
                                    GIMP_VIEWABLE (new_parent));

          gimp_container_insert (new_container, GIMP_OBJECT (item), new_index);

          g_object_unref (item);
        }
      else
        {
          gimp_container_reorder (container, GIMP_OBJECT (item), new_index);
        }
    }

  return TRUE;
}

void
gimp_item_tree_rename_item (GimpItemTree *tree,
                            GimpItem     *item,
                            const gchar  *new_name,
                            gboolean      push_undo,
                            const gchar  *undo_desc)
{
  GimpItemTreePrivate *private;

  g_return_if_fail (GIMP_IS_ITEM_TREE (tree));

  private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  g_return_if_fail (GIMP_IS_ITEM (item));
  g_return_if_fail (gimp_item_is_attached (item));
  g_return_if_fail (gimp_item_get_image (item) == private->image);
  g_return_if_fail (new_name != NULL);

  if (strcmp (new_name, gimp_object_get_name (item)))
    {
      if (push_undo)
        gimp_image_undo_push_item_rename (item->image, undo_desc, item);

      gimp_item_tree_uniquefy_name (tree, item, new_name);
    }
}


/*  private functions  */

static void
gimp_item_tree_uniquefy_name (GimpItemTree *tree,
                              GimpItem     *item,
                              const gchar  *new_name)
{
  GimpItemTreePrivate *private = GIMP_ITEM_TREE_GET_PRIVATE (tree);

  if (new_name)
    {
      g_hash_table_remove (private->name_hash,
                           gimp_object_get_name (item));

      gimp_object_set_name (GIMP_OBJECT (item), new_name);
    }

  while (g_hash_table_lookup (private->name_hash,
                              gimp_object_get_name (item)))
    {
      gchar *name   = g_strdup (gimp_object_get_name (item));
      gchar *ext    = strrchr (name, '#');
      gint   number = 0;

      if (ext)
        {
          gchar *ext_str;

          number = atoi (ext + 1);

          ext_str = g_strdup_printf ("%d", number);

          /*  check if the extension really is of the form "#<n>"  */
          if (! strcmp (ext_str, ext + 1))
            {
              *ext = '\0';
            }
          else
            {
              number = 0;
            }

          g_free (ext_str);
        }

      number++;

      gimp_object_take_name (GIMP_OBJECT (item),
                             g_strdup_printf ("%s#%d", name, number));

      g_free (name);
    }

  g_hash_table_insert (private->name_hash,
                       (gpointer) gimp_object_get_name (item),
                       item);
}