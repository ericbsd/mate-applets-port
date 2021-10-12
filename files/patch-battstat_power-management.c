--- battstat/power-management.c.orig	2021-08-07 11:25:17 UTC
+++ battstat/power-management.c
@@ -63,6 +63,10 @@
 
 static const char *apm_readinfo (BatteryStatus *status);
 static int pm_initialised;
+#ifdef HAVE_HAL
+static int using_hal;
+#endif
+static int using_hal = FALSE;
 #ifdef HAVE_UPOWER
 static int using_upower;
 #endif
@@ -174,16 +178,40 @@ apm_readinfo (BatteryStatus *status)
 
 #elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
 
+#if defined(__i386__)
 #include <machine/apm_bios.h>
+#endif
 #include "acpi-freebsd.h"
 
 static struct acpi_info acpiinfo;
 static gboolean using_acpi;
 static int acpi_count;
+static int acpiwatch;
 static struct apm_info apminfo;
 
+gboolean acpi_freebsd_read(struct apm_info *apminfo, struct acpi_info *acpiinfo);
+
 #define APMDEVICE "/dev/apm"
 
+static gboolean acpi_callback (GIOChannel * chan, GIOCondition cond, gpointer data)
+{
+  gboolean read_error;
+  if (cond & (G_IO_ERR | G_IO_HUP)) {
+    acpi_freebsd_cleanup(&acpiinfo);
+    return FALSE;
+  }
+
+  if (acpi_process_event(&acpiinfo, &read_error)) {
+    acpi_freebsd_read(&apminfo, &acpiinfo);
+  }
+  else if (read_error) {
+    acpi_freebsd_cleanup(&acpiinfo);
+    return FALSE;
+  }
+
+  return TRUE;
+}
+
 static const char *
 apm_readinfo (BatteryStatus *status)
 {
@@ -191,21 +219,28 @@ apm_readinfo (BatteryStatus *status)
 
   if (DEBUG) g_print ("apm_readinfo () (FreeBSD)\n");
 
-  if (using_acpi) {
+  if (using_acpi && (!acpiinfo.event_inited || acpiinfo.event_fd >= 0)) {
     if (acpi_count <= 0) {
       acpi_count = 30;
-      acpi_process_event (&acpiinfo);
-      if (acpi_freebsd_read (&apminfo, &acpiinfo) == FALSE)
+      acpi_process_event(&acpiinfo);
+      if (!acpiinfo.event_inited) {
+        acpi_freebsd_update(&acpiinfo);
+      }
+      if (acpi_freebsd_read(&apminfo, &acpiinfo) == FALSE)
         return ERR_FREEBSD_ACPI;
     }
     acpi_count--;
   }
-  else
-  {
-    /* This is how I read the information from the APM subsystem under
-       FreeBSD.  Each time this functions is called (once every second)
-       the APM device is opened, read from and then closed.
-    */
+  else if (using_acpi && acpiinfo.event_inited) {
+    if (acpi_freebsd_init(&acpiinfo)) {
+      acpiwatch = g_io_add_watch (acpiinfo.channel,
+          G_IO_IN | G_IO_ERR | G_IO_HUP,
+          acpi_callback, NULL);
+      acpi_freebsd_read(&apminfo, &acpiinfo);
+    }
+  }
+  else {
+#if defined(__i386__)
     fd = open (APMDEVICE, O_RDONLY);
     if (fd == -1) {
       return ERR_OPEN_APMDEV;
@@ -218,6 +253,9 @@ apm_readinfo (BatteryStatus *status)
 
     if (apminfo.ai_status == 0)
       return ERR_APM_E;
+#else
+    return ERR_OPEN_APMDEV;
+#endif
   }
 
   status->present = TRUE;
@@ -484,6 +522,12 @@ power_management_initialise (void (*callback) (void))
   }
   else
     using_acpi = FALSE;
+
+  if (using_acpi && acpiinfo.event_fd >= 0) {
+    acpiwatch = g_io_add_watch (acpiinfo.channel,
+       G_IO_IN | G_IO_ERR | G_IO_HUP,
+       acpi_callback, NULL);
+  }
 #endif
   pm_initialised = 1;
 
@@ -517,6 +561,9 @@ power_management_cleanup (void)
   }
 #elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
   if (using_acpi) {
+    if (acpiwatch != 0)
+      g_source_remove(acpiwatch);
+    acpiwatch = 0;
     acpi_freebsd_cleanup (&acpiinfo);
   }
 #endif
@@ -532,5 +579,3 @@ power_management_using_upower (void)
 #else
  return 0;
 #endif
-}
-
