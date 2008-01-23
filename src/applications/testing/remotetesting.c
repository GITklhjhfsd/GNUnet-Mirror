/*
     This file is part of GNUnet.
     (C) 2007 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file applications/testing/testingtest.c
 * @brief testcase for testing library
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_protocols.h"
#include "gnunet_testing_lib.h"

/**
 * Testcase
 * @return 0: ok, -1: error
 */
int
main (int argc, const char **argv)
{  
  static char *configFile = "/tmp/fake.conf";
  static char *path;
  int ret = 0;
  int res;
  struct GNUNET_GC_Configuration *cfg;
  struct GNUNET_GE_Context *ectx;
  

static struct GNUNET_CommandLineOption gnunetstatsOptions[] = {
  GNUNET_COMMAND_LINE_OPTION_CFG_FILE (&configFile),   /* -c */
  GNUNET_COMMAND_LINE_OPTION_HELP (gettext_noop ("Print statistics about GNUnet operations.")), /* -h */
  GNUNET_COMMAND_LINE_OPTION_HOSTNAME,  /* -H */
  GNUNET_COMMAND_LINE_OPTION_LOGGING,   /* -L */
  GNUNET_COMMAND_LINE_OPTION_VERSION (PACKAGE_VERSION), /* -v */
  GNUNET_COMMAND_LINE_OPTION_END,
};

  

  res = GNUNET_init (argc,
                     argv,
                     "testingtest",
                     &configFile, gnunetstatsOptions, &ectx, &cfg);
  if (res == -1)
    {
      GNUNET_fini (ectx, cfg);
      return -1;
    }

  GNUNET_GC_get_configuration_value_filename(cfg,"","CONFIG","",&path);
  
  configFile = strcat(path,configFile);
    
    
    struct GNUNET_GC_Configuration *hostConfig;
    
    if (GNUNET_OK != GNUNET_TESTING_read_config (path,&hostConfig))
    {
    	printf("Problem with main host configuration file...\n");
    	exit(1);	
    }
    
    if (GNUNET_TESTING_check_config(&hostConfig) != GNUNET_OK)
    {
    	printf("Problem with main host configuration file...\n");
    	exit(1);	
    }
                      
	char *one = "/u/home9/natevans/gnunet/GNUnet/";
	char *two = "/tmp/";
	char *three = "/u/home9/natevans/configs/";
	char *four = "130.253.191.151";
	char *five = "natevans";
	char *six = "gnunetd.xxxxx";
	
	
	GNUNET_TESTING_start_single_remote_daemon(one,two,six,three,four,five);
	
	GNUNET_TESTING_parse_config_start_daemons(&hostConfig);
	
  return ret;
}

/* end of testingtest.c */