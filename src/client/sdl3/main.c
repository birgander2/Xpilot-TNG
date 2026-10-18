/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client Main
 */

#include "sdl3_client.h"
#include <signal.h>
#include <time.h>

static void Main_shutdown(void)
{
    Net_cleanup();
    Client_cleanup();
    SDL3_Cleanup();
}

static void sigcatch(int signum)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    Main_shutdown();
    fprintf(stderr, "Got signal %d, exiting.\n", signum);
    exit(1);
}

const char *Program_name(void)
{
    return "xpilot-tng-client";
}

int main(int argc, char *argv[])
{
    bool auto_shutdown = false;

    Conf_enter_bundle_dir();
    init_error(argv[0]);
    seedMT((unsigned)time(NULL) ^ Get_process_id());

    memset(&connectParam, 0, sizeof(Connect_param_t));
    connectParam.contact_port = SERVER_PORT;
    connectParam.team = TEAM_NOT_SET;

    clData.scaleFactor = 1.0;
    clData.scale = 1.0;
    clData.fscale = 1.0f;
    clData.altScaleFactor = 2.0;

    /* Register options */
    Store_default_options();
    Store_talk_macro_options();
    Store_key_options();
    Store_color_options();
    Store_xpaint_options();
    Store_hud_options();
    Store_voice_options();
    Store_paintradar_options();
    Store_guimap_options();
    Store_guiobject_options();

    memset(&xpArgs, 0, sizeof(xp_args_t));
    Parse_options(&argc, argv);

    Config_init();
    SDL3_Init_Colors();
    Init_spark_colors();
    Init_saved_scores();

    if (sock_startup()) {
        fprintf(stderr, "Failed to initialize networking.\n");
        return 1;
    }

    if (SDL3_Init_Window(DEF_TOP_WIDTH, DEF_TOP_HEIGHT) != 0) {
        fprintf(stderr, "Failed to initialize SDL3 window.\n");
        return 1;
    }

    char target_server[128] = "";
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0' && argv[1][0] != '-') {
        strlcpy(target_server, argv[1], sizeof(target_server));
        printf("Connecting to XPilot server: %s...\n", target_server);

        char *srv_ptr = target_server;
        if (!Contact_servers(1, &srv_ptr,
                             true, xpArgs.list_servers,
                             auto_shutdown, xpArgs.shutdown_reason,
                             0, NULL, NULL, NULL, NULL,
                             &connectParam)) {
            fprintf(stderr, "Could not contact server %s.\n", target_server);
            SDL3_Cleanup();
            return 1;
        }
    } else {
        if (SDL3_Run_Welcome(target_server, sizeof(target_server)) != 0) {
            printf("XPilot TNG closed by user.\n");
            SDL3_Cleanup();
            return 0;
        }
    }

    if (Client_init(connectParam.server_name, connectParam.server_version)) {
        fprintf(stderr, "Failed to initialize client logic.\n");
        SDL3_Cleanup();
        return 1;
    }

    if (Net_init(connectParam.server_addr, connectParam.login_port)) {
        fprintf(stderr, "Failed to initialize client net.\n");
        SDL3_Cleanup();
        return 1;
    }

    if (Net_verify(connectParam.user_name, connectParam.nick_name, connectParam.disp_name)) {
        fprintf(stderr, "Failed to verify net connection.\n");
        SDL3_Cleanup();
        return 1;
    }

    if (Net_setup()) {
        fprintf(stderr, "Failed to setup net connection.\n");
        SDL3_Cleanup();
        return 1;
    }

    if (Client_setup()) {
        fprintf(stderr, "Failed to setup client state.\n");
        SDL3_Cleanup();
        return 1;
    }

    signal(SIGINT, sigcatch);
    signal(SIGTERM, sigcatch);

    if (Net_start()) {
        Main_shutdown();
        fprintf(stderr, "Failed to start network.\n");
        return 1;
    }

    if (Client_start()) {
        Main_shutdown();
        fprintf(stderr, "Failed to start client session.\n");
        return 1;
    }

    printf("Connected to server! Starting XPilot TNG game loop...\n");
    SDL3_Game_Loop();

    Main_shutdown();
    return 0;
}
