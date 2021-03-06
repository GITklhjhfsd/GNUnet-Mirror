#include <Qt/qapplication.h>

extern "C" {
  #include "platform.h"
  #include "gnunet_util.h"
  #include "gnunet_directories.h"
  #include "gnunet_setup_lib.h"
}
#include "setupWizard.h"

static struct GNUNET_GE_Context *ectx;
static struct GNUNET_GC_Configuration *cfg;

static void
gns2cfg (struct GNUNET_GNS_TreeNode *pos)
{
  int i;
  char *val;

  if (pos == NULL)
    return;
  i = 0;
  while (pos->children[i] != NULL)
    {
      gns2cfg (pos->children[i]);
      i++;
    }
  if (i != 0)
    return;
  if ((pos->section == NULL) || (pos->option == NULL))
    return;
  if (GNUNET_NO ==
      GNUNET_GC_have_configuration_value (cfg, pos->section, pos->option))
    {
      val = GNUNET_GNS_get_default_value_as_string (pos->type, &pos->value);
      if (val != NULL)
        {
          GNUNET_GC_set_configuration_value_string (cfg,
                                                    ectx,
                                                    pos->section, pos->option,
                                                    val);
          GNUNET_free (val);
        }
    }
}

extern "C" {
int
qt_wizard_mainsetup_qt (int argc,
                          char *const *argv,
                          struct GNUNET_PluginHandle *self,
                          struct GNUNET_GE_Context *ectx,
                          struct GNUNET_GC_Configuration *cfg,
                          struct GNUNET_GNS_Context *gns,
                          const char *filename, int is_daemon)
  {
    QApplication *app;
    GSetupWizard *wiz;
    int ret;
    
    GNUNET_GE_ASSERT (ectx, is_daemon);

#ifdef WINDOWS
  FreeConsole ();
#endif

    app = new QApplication(argc, (char **) argv);

    wiz = new GSetupWizard(NULL, ectx, cfg, filename);
    wiz->show();
    
    ret = app->exec();

    delete wiz;
    delete app;
  
    return ret;
  }
}
