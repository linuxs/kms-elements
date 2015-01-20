/*
 * (C) Copyright 2015 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */

#include "kmswebrtcconnection.h"
#include "kmswebrtctransport.h"
#include <commons/kmsutils.h>

#define GST_CAT_DEFAULT kmswebrtcconnection
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
#define GST_DEFAULT_NAME "kmswebrtcconnection"

#define KMS_WEBRTC_CONNECTION_GET_PRIVATE(obj) (        \
  G_TYPE_INSTANCE_GET_PRIVATE (                         \
    (obj),                                              \
    KMS_TYPE_WEBRTC_CONNECTION,                         \
    KmsWebRtcConnectionPrivate                          \
  )                                                     \
)

struct _KmsWebRtcConnectionPrivate
{
  KmsWebRtcTransport *rtp_tr;
  KmsWebRtcTransport *rtcp_tr;
};

static void kms_webrtc_rtp_connection_interface_init (KmsIRtpConnectionInterface
    * iface);

G_DEFINE_TYPE_WITH_CODE (KmsWebRtcConnection, kms_webrtc_connection,
    KMS_TYPE_WEBRTC_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (KMS_TYPE_I_RTP_CONNECTION,
        kms_webrtc_rtp_connection_interface_init));

static void
kms_webrtc_connection_set_certificate_pem_file (KmsWebRtcBaseConnection
    * base_conn, const gchar * pem)
{
  KmsWebRtcConnection *self = KMS_WEBRTC_CONNECTION (base_conn);
  KmsWebRtcConnectionPrivate *priv = self->priv;

  g_object_set (G_OBJECT (priv->rtp_tr->dtlssrtpdec),
      "certificate-pem-file", pem, NULL);
  g_object_set (G_OBJECT (priv->rtcp_tr->dtlssrtpdec),
      "certificate-pem-file", pem, NULL);
}

static void
add_tr (KmsWebRtcTransport * tr, GstBin * bin, gboolean is_client)
{
  g_object_set (G_OBJECT (tr->dtlssrtpenc), "is-client", is_client, NULL);
  g_object_set (G_OBJECT (tr->dtlssrtpdec), "is-client", is_client, NULL);

  gst_bin_add_many (bin,
      g_object_ref (tr->nicesrc), g_object_ref (tr->dtlssrtpdec), NULL);

  gst_element_link (tr->nicesrc, tr->dtlssrtpdec);

  gst_element_sync_state_with_parent_target_state (tr->dtlssrtpdec);
  gst_element_sync_state_with_parent_target_state (tr->nicesrc);

  {
    gst_bin_add_many (bin,
        g_object_ref (tr->dtlssrtpenc), g_object_ref (tr->nicesink), NULL);

    gst_element_link (tr->dtlssrtpenc, tr->nicesink);
    gst_element_sync_state_with_parent_target_state (tr->nicesink);
    gst_element_sync_state_with_parent_target_state (tr->dtlssrtpenc);
  }
}

static void
kms_webrtc_rtp_connection_add (KmsIRtpConnection * base_rtp_conn, GstBin * bin,
    gboolean local_offer)
{
  KmsWebRtcConnection *self = KMS_WEBRTC_CONNECTION (base_rtp_conn);
  gboolean is_client = !local_offer;

  add_tr (self->priv->rtp_tr, bin, is_client);
  add_tr (self->priv->rtcp_tr, bin, is_client);
}

static GstPad *
kms_webrtc_rtp_connection_request_rtp_sink (KmsIRtpConnection * base_rtp_conn)
{
  KmsWebRtcConnection *self = KMS_WEBRTC_CONNECTION (base_rtp_conn);

  return gst_element_get_static_pad (self->priv->rtp_tr->dtlssrtpenc,
      "rtp_sink");
}

static GstPad *
kms_webrtc_rtp_connection_request_rtp_src (KmsIRtpConnection * base_rtp_conn)
{
  KmsWebRtcConnection *self = KMS_WEBRTC_CONNECTION (base_rtp_conn);

  return gst_element_get_static_pad (self->priv->rtp_tr->dtlssrtpdec, "src");
}

static GstPad *
kms_webrtc_rtp_connection_request_rtcp_sink (KmsIRtpConnection * base_rtp_conn)
{
  KmsWebRtcConnection *self = KMS_WEBRTC_CONNECTION (base_rtp_conn);

  return gst_element_get_static_pad (self->priv->rtcp_tr->dtlssrtpenc,
      "rtcp_sink");
}

static GstPad *
kms_webrtc_rtp_connection_request_rtcp_src (KmsIRtpConnection * base_rtp_conn)
{
  KmsWebRtcConnection *self = KMS_WEBRTC_CONNECTION (base_rtp_conn);

  return gst_element_get_static_pad (self->priv->rtcp_tr->dtlssrtpdec, "src");
}

KmsWebRtcConnection *
kms_webrtc_connection_new (NiceAgent * agent, GMainContext * context,
    const gchar * name)
{
  GObject *obj;
  KmsWebRtcBaseConnection *base_conn;
  KmsWebRtcConnection *conn;
  KmsWebRtcConnectionPrivate *priv;

  obj = g_object_new (KMS_TYPE_WEBRTC_CONNECTION, NULL);
  base_conn = KMS_WEBRTC_BASE_CONNECTION (obj);
  conn = KMS_WEBRTC_CONNECTION (obj);
  priv = conn->priv;

  if (!kms_webrtc_base_connection_configure (base_conn, agent, name)) {
    g_object_unref (obj);
    return NULL;
  }

  priv->rtp_tr =
      kms_webrtc_transport_create (agent, base_conn->stream_id,
      NICE_COMPONENT_TYPE_RTP);
  priv->rtcp_tr =
      kms_webrtc_transport_create (agent, base_conn->stream_id,
      NICE_COMPONENT_TYPE_RTCP);

  if (priv->rtp_tr == NULL || priv->rtcp_tr == NULL) {
    GST_ERROR_OBJECT (obj, "Cannot create KmsWebRTCConnection.");
    g_object_unref (obj);
    return NULL;
  }

  nice_agent_set_stream_name (agent, base_conn->stream_id, name);
  nice_agent_attach_recv (agent, base_conn->stream_id,
      NICE_COMPONENT_TYPE_RTP, context, kms_webrtc_transport_nice_agent_recv_cb,
      NULL);
  nice_agent_attach_recv (agent, base_conn->stream_id, NICE_COMPONENT_TYPE_RTCP,
      context, kms_webrtc_transport_nice_agent_recv_cb, NULL);

  return conn;
}

static void
kms_webrtc_connection_finalize (GObject * object)
{
  KmsWebRtcConnection *self = KMS_WEBRTC_CONNECTION (object);
  KmsWebRtcConnectionPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "finalize");

  kms_webrtc_transport_destroy (priv->rtp_tr);
  kms_webrtc_transport_destroy (priv->rtcp_tr);

  /* chain up */
  G_OBJECT_CLASS (kms_webrtc_connection_parent_class)->finalize (object);
}

static void
kms_webrtc_connection_init (KmsWebRtcConnection * self)
{
  self->priv = KMS_WEBRTC_CONNECTION_GET_PRIVATE (self);
}

static void
kms_webrtc_connection_class_init (KmsWebRtcConnectionClass * klass)
{
  GObjectClass *gobject_class;
  KmsWebRtcBaseConnectionClass *base_conn_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = kms_webrtc_connection_finalize;

  base_conn_class = KMS_WEBRTC_BASE_CONNECTION_CLASS (klass);
  base_conn_class->set_certificate_pem_file =
      kms_webrtc_connection_set_certificate_pem_file;

  g_type_class_add_private (klass, sizeof (KmsWebRtcConnectionPrivate));

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
      GST_DEFAULT_NAME);
}

static void
kms_webrtc_rtp_connection_interface_init (KmsIRtpConnectionInterface * iface)
{
  iface->add = kms_webrtc_rtp_connection_add;
  iface->request_rtp_sink = kms_webrtc_rtp_connection_request_rtp_sink;
  iface->request_rtp_src = kms_webrtc_rtp_connection_request_rtp_src;
  iface->request_rtcp_sink = kms_webrtc_rtp_connection_request_rtcp_sink;
  iface->request_rtcp_src = kms_webrtc_rtp_connection_request_rtcp_src;
}
