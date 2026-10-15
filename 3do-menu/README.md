# Menu 3DO pour SataTo3DO

Application 3DO (Opera) qui liste les jeux presents sur la cle USB du module.

## Contenu
- `Menu.c` : code source complet (liste, defilement vertical par page,
  marquee horizontal pour les noms longs, lancement via commande ODE).

## Compilation
1. Installer l'Opera SDK / toolchain `arm-elf-gcc` (ex. 3DO SDK de pan069 ou
   la chaine utilisee pour compiler le "maduck" boot d'origine).
2. Compiler et editer les liens avec `romlib` :
   `arm-elf-gcc -O2 Menu.c -o LaunchMe -lrom ...`
3. Embarquer `LaunchMe` + polices/assets dans une image PFS
   (`pfs_floppy` / `maduck`) pour produire un nouveau `boot.iso`.

## Script de boot (start.script du pfs)
```
minmem
$drivers/ode&
$boot/LaunchMe
```

## Touches
| Touche | Action |
|--------|--------|
| Haut/Bas | Curseur (le nom long defile automatiquement) |
| L/R | Page precedente / suivante |
| A | Charger le disque selectionne |
| Start | Eject (lecteur physique) |

## Notes
- Le defilement horizontal ("marquee") s'active quand le nom depasse
  `VISIBLE_CHARS` caracteres; il revient a zero a chaque changement de ligne.
- La bascule de disque passe par la commande ODE privee du firmware
  (`CSC_ODE_CHANGE_DISC`, voir `src/interfaces/3DO.c` du SataTo3DO) :
  ajustez l'IOCTL/code et la structure selon la version du firmware installee.
