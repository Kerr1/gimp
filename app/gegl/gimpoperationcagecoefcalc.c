/* GIMP - The GNU Image Manipulation Program
 *
 * gimpoperationcagecoefcalc.c
 * Copyright (C) 2010 Michael Muré <batolettre@gmail.com>
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

#include <gegl.h>
#include "gimp-gegl-types.h"
#include <gegl-buffer-iterator.h>

#include "libgimpmath/gimpmath.h"

#include "gimpoperationcagecoefcalc.h"
#include "gimpcageconfig.h"


static void           gimp_operation_cage_coef_calc_prepare           (GeglOperation        *operation);
static void           gimp_operation_cage_coef_calc_finalize          (GObject              *object);
static void           gimp_operation_cage_coef_calc_get_property      (GObject              *object,
                                                                       guint                 property_id,
                                                                       GValue               *value,
                                                                       GParamSpec           *pspec);
static void           gimp_operation_cage_coef_calc_set_property      (GObject              *object,
                                                                       guint                 property_id,
                                                                       const GValue         *value,
                                                                       GParamSpec           *pspec);
static GeglRectangle  gimp_operation_cage_coef_calc_get_bounding_box  (GeglOperation        *operation);
static gboolean       gimp_operation_cage_coef_calc_process           (GeglOperation        *operation,
                                                                       GeglBuffer           *output,
                                                                       const GeglRectangle  *roi);


G_DEFINE_TYPE (GimpOperationCageCoefCalc, gimp_operation_cage_coef_calc,
               GEGL_TYPE_OPERATION_SOURCE)

#define parent_class gimp_operation_cage_coef_calc_parent_class


static void
gimp_operation_cage_coef_calc_class_init (GimpOperationCageCoefCalcClass *klass)
{
  GObjectClass             *object_class    = G_OBJECT_CLASS (klass);
  GeglOperationSourceClass *source_class    = GEGL_OPERATION_SOURCE_CLASS (klass);
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);

  /* FIXME: wrong categories and name, to appears in the gegl tool */
  operation_class->name               = "gimp:cage_coef_calc";
  operation_class->categories         = "transform";
  operation_class->description        = "GIMP cage transform coefficient calc";

  operation_class->prepare            = gimp_operation_cage_coef_calc_prepare;
  operation_class->get_bounding_box   = gimp_operation_cage_coef_calc_get_bounding_box;
  operation_class->no_cache           = FALSE;
  operation_class->get_cached_region  = NULL;

  source_class->process               = gimp_operation_cage_coef_calc_process;

  object_class->get_property          = gimp_operation_cage_coef_calc_get_property;
  object_class->set_property          = gimp_operation_cage_coef_calc_set_property;
  object_class->finalize              = gimp_operation_cage_coef_calc_finalize;

  g_object_class_install_property (object_class,
                                   GIMP_OPERATION_CAGE_COEF_CALC_PROP_CONFIG,
                                   g_param_spec_object ("config", NULL, NULL,
                                                        GIMP_TYPE_CAGE_CONFIG,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));
}

static void
gimp_operation_cage_coef_calc_init (GimpOperationCageCoefCalc *self)
{

}

static void
gimp_operation_cage_coef_calc_prepare (GeglOperation *operation)
{
  GimpOperationCageCoefCalc   *occc   = GIMP_OPERATION_CAGE_COEF_CALC (operation);
  GimpCageConfig              *config = GIMP_CAGE_CONFIG (occc->config);

  gegl_operation_set_format (operation, "output", babl_format_n (babl_type ("float"), 2 * config->cage_vertice_number));
}

static void
gimp_operation_cage_coef_calc_finalize (GObject *object)
{
  GimpOperationCageCoefCalc *self = GIMP_OPERATION_CAGE_COEF_CALC (object);

  if (self->config)
    {
      g_object_unref (self->config);
      self->config = NULL;
    }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gimp_operation_cage_coef_calc_get_property  (GObject    *object,
                                             guint       property_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  GimpOperationCageCoefCalc   *self   = GIMP_OPERATION_CAGE_COEF_CALC (object);

  switch (property_id)
    {
    case GIMP_OPERATION_CAGE_COEF_CALC_PROP_CONFIG:
      g_value_set_object (value, self->config);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gimp_operation_cage_coef_calc_set_property  (GObject      *object,
                                             guint         property_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  GimpOperationCageCoefCalc   *self   = GIMP_OPERATION_CAGE_COEF_CALC (object);

  switch (property_id)
    {
    case GIMP_OPERATION_CAGE_COEF_CALC_PROP_CONFIG:
      if (self->config)
        g_object_unref (self->config);
      self->config = g_value_dup_object (value);
      break;

   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static gboolean
gimp_operation_cage_coef_calc_is_on_straight (GimpVector2 *d1,
                                              GimpVector2 *d2,
                                              GimpVector2 *p)
{
  GimpVector2 v1, v2;
  gfloat deter;

  v1.x = p->x - d1->x;
  v1.y = p->y - d1->y;
  v2.x = d2->x - d1->x;
  v2.y = d2->y - d1->y;

  gimp_vector2_normalize (&v1);
  gimp_vector2_normalize (&v2);

  deter = v1.x * v2.y - v2.x * v1.y;

  return (deter < 0.000000001) && (deter > -0.000000001);
}

static GeglRectangle
gimp_operation_cage_coef_calc_get_bounding_box (GeglOperation *operation)
{
  GimpOperationCageCoefCalc   *occc   = GIMP_OPERATION_CAGE_COEF_CALC (operation);
  GimpCageConfig              *config = GIMP_CAGE_CONFIG (occc->config);

  return gimp_cage_config_get_bounding_box (config);
}

static gboolean
gimp_operation_cage_coef_calc_process (GeglOperation       *operation,
                                       GeglBuffer          *output,
                                       const GeglRectangle *roi)
{
  GimpOperationCageCoefCalc   *occc   = GIMP_OPERATION_CAGE_COEF_CALC (operation);
  GimpCageConfig              *config = GIMP_CAGE_CONFIG (occc->config);

  Babl *format = babl_format_n (babl_type ("float"), 2 * config->cage_vertice_number);

  GeglBufferIterator *it;

  if (! config)
    return FALSE;

  it = gegl_buffer_iterator_new (output, roi, format, GEGL_BUFFER_READWRITE);

  while (gegl_buffer_iterator_next (it))
  {
    /* iterate inside the roi */
    gint  n_pixels = it->length;
    gint  x = it->roi->x; /* initial x                   */
    gint  y = it->roi->y; /*           and y coordinates */
    gint  j;

    gfloat      *coef = it->data[0];

    while(n_pixels--)
    {
      if (gimp_cage_config_point_inside(config, x, y))
      {
        for( j = 0; j < config->cage_vertice_number; j++)
        {
          GimpVector2 v1,v2,a,b,p;
          gfloat Q,S,R,BA,SRT,L0,L1,A0,A1,A10,L10;

          v1 = config->cage_vertices[j];
          v2 = config->cage_vertices[(j+1)%config->cage_vertice_number];
          p.x = x;
          p.y = y;

          a.x = v2.x - v1.x;
          a.y = v2.y - v1.y;
          b.x = v1.x - x;
          b.y = v1.y - y;
          Q = a.x * a.x + a.y * a.y;
          S = b.x * b.x + b.y * b.y;
          R = 2.0 * (a.x * b.x + a.y * b.y);
          BA = b.x * a.y - b.y * a.x;
          SRT = sqrt(4.0 * S * Q - R * R);

          L0 = log(S);
          L1 = log(S + Q + R);
          A0 = atan2(R, SRT) / SRT;
          A1 = atan2(2.0 * Q + R, SRT) / SRT;
          A10 = A1 - A0;
          L10 = L1 - L0;

          /* edge coef */
          coef[j + config->cage_vertice_number] = (1.0 / (4.0 * M_PI)) * ((4.0*S-(R*R)/Q) * A10 + (R / (2.0 * Q)) * L10 + L1 - 2.0);

          if (isnan(coef[j + config->cage_vertice_number]))
          {
            coef[j + config->cage_vertice_number] = 0.0;
          }

          /* vertice coef */
          if (!gimp_operation_cage_coef_calc_is_on_straight (&v1, &v2, &p))
          {
            coef[j] += (BA / (2.0 * M_PI)) * (L10 /(2.0*Q) - A10 * (2.0 + R / Q));
            coef[(j+1)%config->cage_vertice_number] -= (BA / (2.0 * M_PI)) * (L10 / (2.0 * Q) - A10 * (R / Q));
          }
        }
      }

      coef += 2 * config->cage_vertice_number;

      /* update x and y coordinates */
      x++;
      if (x >= (it->roi->x + it->roi->width))
      {
        x = it->roi->x;
        y++;
      }
    }
  }

  return TRUE;
}