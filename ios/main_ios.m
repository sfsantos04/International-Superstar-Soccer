// main_ios.m - iOS entry point for ISSDNative.
//
// The upstream main() lives in ISSDNative/main.c and is compiled with
// -Dmain=issd_main. On iOS the process entry belongs to UIKit, and SDL owns it
// through SDL_main (libSDL2main.a calls UIApplicationMain, which calls this).
// So this file provides SDL_main, finds the cartridge inside the app's
// Documents folder (visible in the Files app), builds the argv the CLI expects
// and hands control to the same code path the desktop builds use.
#import <Foundation/Foundation.h>
#include <SDL.h>
#include <string.h>
#include <stdio.h>

extern int issd_main(int argc, char **argv);

int SDL_main(int argc, char **argv)
{
    @autoreleasepool {
        NSArray *dirs = NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *docs = dirs.count ? dirs[0] : nil;
        if (!docs) return 2;

        NSString *rom = nil;
        NSArray *files = [[NSFileManager defaultManager]
                          contentsOfDirectoryAtPath:docs error:nil];
        for (NSString *f in files) {
            NSString *low = [f lowercaseString];
            if ([low hasSuffix:@".sfc"] || [low hasSuffix:@".smc"] ||
                [low hasSuffix:@".fig"]) {
                rom = [docs stringByAppendingPathComponent:f];
                break;
            }
        }

        static char rombuf[4096];
        static char *cargv[8];
        int n = 0;
        cargv[n++] = argv[0];
        if (rom) {
            strncpy(rombuf, [rom fileSystemRepresentation], sizeof(rombuf) - 1);
            cargv[n++] = rombuf;
        } else {
            fprintf(stderr, "[ISSD-iOS] no .sfc/.smc in Documents\n");
        }
        cargv[n++] = (char *)"--no-launcher";   /* skip the console launcher, go straight to the game */
        cargv[n] = NULL;
        return issd_main(n, cargv);
    }
}
