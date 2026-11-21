// main_ios.m - iOS entry point for ISSDNative.
//
// The upstream main() lives in ISSDNative/main.c and is compiled with
// -Dmain=issd_main. On iOS the process entry belongs to UIKit, and SDL owns it
// through SDL_main (libSDL2main.a calls UIApplicationMain, which calls this).
//
// Everything the game prints goes to Documents/issd-log.txt, because on a
// device that stream is otherwise discarded and a black screen gives no clue.
// The log sits in the Files app, next to the ROM.
#import <Foundation/Foundation.h>
#include <SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

extern int issd_main(int argc, char **argv);

static FILE *g_log = NULL;

static void lg(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

/* Hunt for a cartridge: Documents itself, then one level of subfolders, which
 * is how a file copied in through the Files app sometimes lands. */
static NSString *find_rom(NSString *docs, NSFileManager *fm)
{
    NSArray *files = [fm contentsOfDirectoryAtPath:docs error:nil];
    for (NSString *f in files) {
        NSString *low = [f lowercaseString];
        if ([low hasSuffix:@".sfc"] || [low hasSuffix:@".smc"] ||
            [low hasSuffix:@".fig"] || [low hasSuffix:@".bin"])
            return [docs stringByAppendingPathComponent:f];
    }
    for (NSString *f in files) {
        NSString *sub = [docs stringByAppendingPathComponent:f];
        BOOL isDir = NO;
        if ([fm fileExistsAtPath:sub isDirectory:&isDir] && isDir) {
            for (NSString *g in [fm contentsOfDirectoryAtPath:sub error:nil]) {
                NSString *low = [g lowercaseString];
                if ([low hasSuffix:@".sfc"] || [low hasSuffix:@".smc"] ||
                    [low hasSuffix:@".fig"] || [low hasSuffix:@".bin"])
                    return [sub stringByAppendingPathComponent:g];
            }
        }
    }
    return nil;
}

int SDL_main(int argc, char **argv)
{
    @autoreleasepool {
        NSArray *dirs = NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *docs = dirs.count ? dirs[0] : NSTemporaryDirectory();

        g_log = fopen([[docs stringByAppendingPathComponent:@"issd-log.txt"]
                       fileSystemRepresentation], "w");
        if (!g_log) g_log = stderr;

        /* From here on whatever the game prints lands in the log as well. */
        if (g_log != stderr) {
            dup2(fileno(g_log), STDOUT_FILENO);
            dup2(fileno(g_log), STDERR_FILENO);
            setvbuf(stdout, NULL, _IOLBF, 0);
            setvbuf(stderr, NULL, _IOLBF, 0);
        }

        lg("=== ISSD iOS ===\n");
        lg("Documents: %s\n", [docs fileSystemRepresentation]);
        lg("bundle:    %s\n", [[[NSBundle mainBundle] bundlePath] UTF8String]);

        /* The bundle is read-only on iOS: point HOME/TMPDIR at Documents so a
         * config or save written by the game does not fail silently. */
        setenv("HOME", [docs fileSystemRepresentation], 1);
        setenv("TMPDIR", [[docs stringByAppendingPathComponent:@"tmp"]
                          fileSystemRepresentation], 1);

        /* The bundle is read-only, so a config saved from the pause menu would
         * be silently dropped. Mirror the assets into Documents and run from
         * there: settings, HD packs and saves then persist between launches. */
        NSString *bundle = [[NSBundle mainBundle] bundlePath];
        NSFileManager *fmw = [NSFileManager defaultManager];

        NSString *recomp = [docs stringByAppendingPathComponent:@"recomp"];
        [fmw createDirectoryAtPath:recomp withIntermediateDirectories:YES
                        attributes:nil error:nil];
        NSString *denySrc = [bundle stringByAppendingPathComponent:@"aot_boot_deny.txt"];
        NSString *denyDst = [recomp stringByAppendingPathComponent:@"aot_boot_deny.txt"];
        if (![fmw fileExistsAtPath:denyDst] && [fmw fileExistsAtPath:denySrc])
            [fmw copyItemAtPath:denySrc toPath:denyDst error:nil];

        NSString *cfgSrc = [bundle stringByAppendingPathComponent:@"issd_config.json"];
        NSString *cfgDst = [docs stringByAppendingPathComponent:@"issd_config.json"];
        if (![fmw fileExistsAtPath:cfgDst] && [fmw fileExistsAtPath:cfgSrc])
            [fmw copyItemAtPath:cfgSrc toPath:cfgDst error:nil];

        NSString *modsDst = [docs stringByAppendingPathComponent:@"mods"];
        NSString *modsSrc = [bundle stringByAppendingPathComponent:@"mods"];
        if (![fmw fileExistsAtPath:modsDst] && [fmw fileExistsAtPath:modsSrc])
            [fmw createSymbolicLinkAtPath:modsDst withDestinationPath:modsSrc error:nil];

        if (chdir([docs fileSystemRepresentation]) != 0)
            lg("chdir(Documents) falhou: %s\n", strerror(errno));
        lg("cwd gravavel: %s\n", [[[NSFileManager defaultManager]
              currentDirectoryPath] UTF8String]);

        NSFileManager *fm = [NSFileManager defaultManager];
        NSArray *files = [fm contentsOfDirectoryAtPath:docs error:nil];
        lg("arquivos em Documents (%lu):\n", (unsigned long)files.count);
        for (NSString *f in files) lg("  - %s\n", [f UTF8String]);

        NSString *rom = find_rom(docs, fm);
        static char rombuf[4096];
        static char *cargv[8];
        int n = 0;
        cargv[n++] = argv[0];

        if (rom) {
            NSDictionary *st = [fm attributesOfItemAtPath:rom error:nil];
            unsigned long long size = [st fileSize];
            lg("ROM: %s\n", [rom fileSystemRepresentation]);
            lg("tamanho: %llu bytes (0x%llX)\n", size, size);
            if (size == 2097152ull)      lg("forma: 2 MB sem header - aceita\n");
            else if (size == 2097664ull) lg("forma: 2 MB com header de 512 - aceita\n");
            else lg("ATENCAO: o loader exige 2097152 ou 2097664 bytes. "
                    "Este arquivo sera recusado.\n");

            NSData *head = [NSData dataWithContentsOfFile:rom
                              options:NSDataReadingMappedIfSafe error:nil];
            if (head.length >= 32) {
                const unsigned char *b = head.bytes;
                char hex[100]; int p = 0;
                for (int i = 0; i < 32; i++)
                    p += snprintf(hex + p, sizeof hex - p, "%02X", b[i]);
                lg("primeiros 32 bytes: %s\n", hex);
            }
            strncpy(rombuf, [rom fileSystemRepresentation], sizeof(rombuf) - 1);
            cargv[n++] = rombuf;
        } else {
            lg("ERRO: nenhum cartucho encontrado em Documents\n");
        }
        cargv[n++] = (char *)"--no-launcher";
        cargv[n] = NULL;

        lg("chamando issd_main com %d argumentos\n", n);
        int rc = issd_main(n, cargv);
        lg("issd_main retornou %d\n", rc);
        if (g_log) fflush(g_log);
        return rc;
    }
}
