/*
 * Menu.c -- Listeur de fichiers USB pour SataTo3DO
 * ------------------------------------------------
 * Application 3DO (Opera/GoldStar) qui affiche la liste des jeux (.iso/.bin)
 * presents a la racine du disque USB branche sur le module SataTo3DO,
 * avec :
 *   - defilement vertical par page (curseur haut/bas, L/R = page)
 *   - defilement horizontal ("marquee") pour les noms trop longs
 *   - selection d'un disque via la commande ODE "change disc"
 *
 * Compile avec l'Opera SDK (arm-elf-gcc), edition de liens avec romlib.
 * Script de demarrage (pfs_floppy / start.script) :
 *      minmem
 *      $drivers/ode&
 *      $boot/LaunchMe
 */

#include <rom.h>
#include <mem.h>
#include <event.h>
#include <graphics.h>
#include <item.h>
#include <font.h>
#include <fileio.h>
#include <dsf.h>          /* FileListCreate / FileListQuery ... */
#include <mdata.h>        /* TagItem definitions */

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */
#define MENU_TITLE      "SataTo3DO - Jeux USB"
#define MAX_ENTRIES     256         /* nb max de fichiers listes        */
#define LINE_PER_PAGE   12          /* lignes visibles a l'ecran        */
#define NAME_MAXLEN     48          /* taille du buffer de nom          */
#define VISIBLE_CHARS   28          /* caracteres affiches par ligne    */
#define SCROLL_DELAY    25          /* frames entre 1 crans de scroll   */
#define LINE_H          20          /* hauteur d'une ligne              */
#define X_TEXT          40
#define Y_FIRST_LINE    90

typedef struct {
    char name[NAME_MAXLEN];         /* nom du fichier                   */
    uint32 sector;                  /* secteur de debut (pour changeDisc)*/
} Entry;

/* Etat global */
static Item      gScreenItem, gPixMapItem, gFontItem;
static Entry    *gEntries = NULL;
static int32     gEntryCount   = 0;
static int32     gCursor       = 0;   /* ligne selectionnee              */
static int32     gTopLine      = 0;   /* premiere ligne affichee         */
static int32     gScrollOffset = 0;   /* decalage horizontal courant     */
static int32     gScrollTick   = 0;   /* compteur de frames              */
static int32     gScrollDir    = 1;   /* sens du marquee                 */

/* ------------------------------------------------------------------ */
/* Lecture du TOC via le driver ODE                                    */
/* Le firmware repond a la requete LOADRUN/TOC : on passe par un       */
/* IORequest special au device "$drivers/ode" (voir MSC/CDROM du fw).  */
/* Ici on utilise GetFileSysStatus + DSF pour lister le volume monte.  */
/* ------------------------------------------------------------------ */
static errcode ScanUsbFiles(void)
{
    Item dirItem;
    struct DirComp dc;
    Entry *tmp;
    int32 n = 0;

    tmp = AllocMem(sizeof(Entry) * MAX_ENTRIES, MEMTYPE_ANY);
    if (!tmp) return ERR_NO_MEMORY;

    /* On liste la racine du disque (monte par le systeme sous
       son label, ou "$" si autoloop ODE). Les extensions acceptees
       sont .ISO et .BIN (le firmware gere aussi .CUE/.MP3). */
    dirItem = OpenDisk("$");
    if (dirItem < 0) {
        /* fallback : certains firmwares exposent la liste complete
           via une pseudo-commande ; on garde la methode DSF standard */
        FreeMem(tmp, sizeof(Entry) * MAX_ENTRIES);
        return dirItem;
    }

    dc.dc_Flags = DCS_CURRENT | DCS_DIRECTORY;
    while (ReadDirBlock(dirItem, &dc) == 0 && n < MAX_ENTRIES) {
        const char *nm = (char *)dc.dc_Name;
        int32 len = StrLen(nm);
        if (len > 3) {
            const char *ext = nm + len - 4;
            if (CompareString(ext, ".ISO", 4) == 0 ||
                CompareString(ext, ".BIN", 4) == 0) {
                StrCopy(tmp[n].name, nm);
                tmp[n].sector = 0;    /* resolu par le firmware */
                n++;
            }
        }
    }
    CloseDisk(dirItem);

    gEntries = tmp;
    gEntryCount = n;
    return 0;
}

/* Tri simple (alphabetique) */
static void SortEntries(void)
{
    for (int32 i = 1; i < gEntryCount; i++) {
        Entry e = gEntries[i];
        int32 j = i - 1;
        while (j >= 0 && CompareString(gEntries[j].name, e.name, TAG_IGNORE) > 0) {
            gEntries[j + 1] = gEntries[j];
            j--;
        }
        gEntries[j + 1] = e;
    }
}

/* ------------------------------------------------------------------ */
/* Affichage                                                           */
/* ------------------------------------------------------------------ */
static void DrawFrame(void)
{
    Item temp;
    uint32 bg = MakeRGB15(2, 2, 20);
    uint32 fg = MakeRGB15(255, 255, 255);
    uint32 hl = MakeRGB15(255, 200, 0);

    temp = GetVWrapItemInfo(TAG_DONE);
    InitTempItem(gScreenItem, temp);
    ClearPixMap(temp, MakePixelItem(bg, 0));

    SetFOVColor(fg);
    PrintAt(10, 30, MENU_TITLE, temp);

    for (int32 row = 0; row < LINE_PER_PAGE; row++) {
        int32 idx = gTopLine + row;
        if (idx >= gEntryCount) break;

        char buf[VISIBLE_CHARS + 1];
        Entry *e = &gEntries[idx];
        int32 nlen = StrLen(e->name);

        /* defilement horizontal : on ne fait bouger que la ligne courante */
        int32 off = (idx == gCursor && nlen > VISIBLE_CHARS) ? gScrollOffset : 0;
        int32 cnt = VISIBLE_CHARS;
        if (off + cnt > nlen) cnt = nlen - off;
        if (cnt < 0) cnt = 0;

        /* fond de la ligne selectionnee */
        if (idx == gCursor) {
            FillRect(X_TEXT - 8, Y_FIRST_LINE + row * LINE_H,
                     VISIBLE_CHARS * 8 + 16, LINE_H, MakePixelItem(hl, 0));
        }

        CopyMem(e->name + off, buf, cnt);
        buf[cnt] = 0;
        /* triangle de curseur + fleche si texte tronque */
        PrintAt(X_TEXT - 20, Y_FIRST_LINE + row * LINE_H,
                (idx == gCursor) ? ">" : " ", temp);
        PrintAt(X_TEXT, Y_FIRST_LINE + row * LINE_H, buf, temp);
        if (nlen > VISIBLE_CHARS && idx == gCursor)
            PrintAt(X_TEXT + VISIBLE_CHARS * 8, Y_FIRST_LINE + row * LINE_H,
                    (gScrollOffset + VISIBLE_CHARS < nlen) ? ">>" : "<<", temp);
    }

    /* pied de page : nb disques + aide touches */
    {
        char foot[64];
        PrintAt(10, 56, "A: jeu  START: eject  UP/DOWN: choix", temp);
        sprintf(foot, "Disque %ld/%ld", gCursor + 1, gEntryCount);
        PrintAt(GetScreenWidth() - 160, 56, foot, temp);
    }

    /* double buffering */
    {
        Item new = TempItemToNewRealItem(temp, TAG_DONE);
        if (new >= 0) {
            ShowItem(new, gScreenItem, DISPLAY_NOW, TRANS_NONE);
            if (gScreenItem != gPixMapItem) { /* libere l'ancien back */ }
            gScreenItem = new;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Choix d'un disque : commande ODE "change disc"                      */
/* Le firmware SataTo3DO attend la sequence :                           */
/*   SendCommand(0x0E) select track / puis LOADRUN du pfs du nouveau    */
/*   disque. On delegue au noyau en remountant la racine.               */
/* ------------------------------------------------------------------ */
static void LaunchSelected(void)
{
    /* 1. demander au firmware de basculer sur l'ISO selectionne */
    IOReq *iorq = CreateIOReq(NULL, 0, "$drivers/ode", 0);
    if (iorq >= 0) {
        struct DiskChangeArgs dca;   /* structure specifique ODE */
        dca.DCA_Sector = gEntries[gCursor].sector;
        dca.DCA_Length = 0;
        dca.DCA_DataPv = gEntries[gCursor].name;
        dca.DCA_DataLg = NAME_MAXLEN;
        SendIOReq(iorq, NULL, IODIR_GENERAL | IOC_ODE | CSC_ODE_CHANGE_DISC, &dca);
        DeleteIOReq(iorq, NULL, 0, 0);
    }
    /* 2. relancer le boot : le firmware sert le nouveau disque */
    LoadRun("$boot/LaunchMe", NULL, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Gestion des evenements                                              */
/* ------------------------------------------------------------------ */
static void HandlePad(uint32 buttons)
{
    static uint32 prev = 0;
    uint32 pressed = buttons & ~prev;
    prev = buttons;

    if (pressed & PAD_UP) {
        if (gCursor > 0) gCursor--;
        if (gCursor < gTopLine) gTopLine = gCursor;
        gScrollOffset = 0; gScrollTick = 0; gScrollDir = 1;
    }
    if (pressed & PAD_DOWN) {
        if (gCursor < gEntryCount - 1) gCursor++;
        if (gCursor >= gTopLine + LINE_PER_PAGE) gTopLine = gCursor - LINE_PER_PAGE + 1;
        gScrollOffset = 0; gScrollTick = 0; gScrollDir = 1;
    }
    if (pressed & PAD_LEFT)  { gTopLine -= LINE_PER_PAGE; if (gTopLine < 0) gTopLine = 0; gCursor = gTopLine; }
    if (pressed & PAD_RIGHT) { gTopLine += LINE_PER_PAGE; if (gTopLine + LINE_PER_PAGE > gEntryCount) gTopLine = (gEntryCount ? gEntryCount - LINE_PER_PAGE : 0); gCursor = gTopLine; }
    if (pressed & PAD_A)     { if (gEntryCount) LaunchSelected(); }
    if (pressed & PAD_START) { /* eject : revient au lecteur physique */ }
}

/* Marquee : avance d'un caractere toutes les SCROLL_DELAY frames */
static void UpdateMarquee(void)
{
    if (!gEntryCount) return;
    int32 nlen = StrLen(gEntries[gCursor].name);
    if (nlen <= VISIBLE_CHARS) { gScrollOffset = 0; return; }

    if (++gScrollTick >= SCROLL_DELAY) {
        gScrollTick = 0;
        gScrollOffset += gScrollDir;
        if (gScrollOffset + VISIBLE_CHARS >= nlen + 2) gScrollDir = -1; /* pause fin */
        else if (gScrollOffset <= 0) { gScrollDir = 1; }
        if (gScrollOffset < 0) gScrollOffset = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    Item evbroker;
    EventMsg *msg;

    gPixMapItem = gScreenItem = OpenMainDisplayItem();
    gFontItem   = OpenFont("assets/sans_serif_14.font", NULL, 0);
    if (gFontItem < 0) gFontItem = OpenFont("$fonts/sans_serif_14.font", NULL, 0);
    UseFontItem(gFontItem, TAG_DONE);

    if (ScanUsbFiles() < 0 || gEntryCount == 0) {
        /* ecran vide : aucun ISO trouve */
        PrintAt(20, 30, "Aucun jeu (.ISO/.BIN) sur la cle USB", gScreenItem);
        DelayFrames(60 * 5);
        return 0;
    }
    SortEntries();

    evbroker = CreateEventBroker(NULL, IT_NULL, 10,
                                 EB_WAIT_ALL,
                                         SIGNAL_0, NULL, TAG_DONE);
    AddControlPad(evbroker, 0, NULL, 0);

    while (1) {
        msg = WaitEvent(evbroker);
        if (msg) {
            switch (msg->Type) {
                case ET_CONTROL_PAD:
                    HandlePad(msg->Buttons);
                    break;
                default:
                    break;
            }
            ReplyEventMsg(evbroker, msg);
        }
        UpdateMarquee();
        DrawFrame();
        DelayFrames(1);
    }
    return 0;
}
