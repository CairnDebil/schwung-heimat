# Heimat 0.7.0 — scheda di collaudo della beta

Grazie per il collaudo. Heimat ha superato tutti i test offline, ma non ha
ancora passato ore su una Move vera: questa scheda serve a questo. Ogni sezione
richiede 5–15 minuti; fai quelle che puoi, nell'ordine che vuoi, e spunta quello
che funziona. Quando qualcosa non va, apri una segnalazione **Beta report** (il
modulo chiede tutto quello che serve, vedi in fondo).

Per tutto ciò che registra usa le cuffie o un cavo nell'ingresso di linea: con
gli altoparlanti interni il microfono sente gli altoparlanti.

## 0. Prima di cominciare

- [ ] Annota la versione del **firmware della Move** e quella di **Schwung**.
- [ ] Installa i due pacchetti (Schwung Manager → *Modules* → modulo
      personalizzato, oppure `scripts/install.sh`) e riavvia la Move.
- [ ] Apri Heimat Solo → **Menu** → **About / DSP load**: deve dire
      `HEIMAT 0.7.0 BETA`. Tieni a mente questa schermata: serve per le misure
      di carico.

**Come leggere About / DSP load.** `DSP` è la parte media di ogni blocco audio
che Heimat usa da solo, `PEAK` il blocco più pesante degli ultimi secondi,
`OVER` quanti blocchi hanno impiegato più del tempo reale da quando Heimat è
partito. Si misura solo Heimat, non il resto della Move. `OVER` sopra 0 vuol
dire che probabilmente c'è stato un click o un buco: segnalalo sempre, insieme
a cosa stava suonando.

## 1. Caricare e suonare (Heimat Solo)

- [ ] Carica WAV di tipi diversi: 16 e 24 bit, 32 bit float, mono e stereo,
      44,1 e 48 kHz, uno con i marker (cue). Ognuno suona all'altezza e alla
      velocità giuste; i marker diventano splice.
- [ ] Carica un file lungo (2–3 minuti) **mentre qualcosa sta suonando**:
      nessun click o buco durante il caricamento. I file oltre 2,9 minuti
      vengono tagliati lì.
- [ ] Pad in modalità Pitch, Splice e Drone; i tasti step 1–16 scelgono le
      splice; Sinistra/Destra e Su/Giù fanno quello che dice il README.
- [ ] Lo schermo si legge: nessun testo tagliato o sovrapposto, la lettura del
      knob non copre mai la forma d'onda.

## 2. Registrazione

- [ ] Registra in una splice nuova (Shift + Record o Sample), dal microfono e
      dall'ingresso di linea; Input Gain si comporta bene.
- [ ] Registra sopra una splice; Sound on Sound da 0 (solo ingresso) a 1.
- [ ] Con il clock in movimento e Record Sync acceso, la registrazione aspetta
      il tick successivo per partire e per fermarsi.
- [ ] Salva la bobina (Menu → Save reel): compare in
      `UserLibrary/Samples/Heimat` con i suoi marker; ricaricala.
- [ ] Undo dopo una registrazione in splice nuova e dopo una modifica ai marker.

## 3. Geni e Micro

- [ ] Pagina PLAY: Gene-Size, Slide, Morph, Vari-Speed (anche all'indietro e
      fermo), Organize.
- [ ] Pagina MICRO: ogni inviluppo, Scatter, Spread, Width, Selective e
      Threshold.
- [ ] **Gene Reverse (GREV)**: al 50% si sentono geni in avanti e all'indietro
      mescolati; al 100% vanno tutti all'indietro, anche con Vari-Speed
      all'indietro (e allora suonano in avanti).
- [ ] **Spread Intervals (INTV)**: Spread a 12 e Octaves → tre ottave nette;
      Fifths, Major, Minor → accordi riconoscibili; Free → una sfocatura.

## 4. Effetti e HEADS

- [ ] EQ, delay (libero e sincronizzato), ping-pong, riverbero: tutto al
      massimo per un minuto. Forte, magari selvaggio, ma niente che esplode e
      niente silenzio dopo.
- [ ] HEADS: ogni Routing e Mode, ogni Wear al feedback massimo; Freeze tiene
      il nastro; *Clear heads* le zittisce subito.
- [ ] Le testine seguono il tempo: cambia il BPM interno e il tempo della Move.

## 5. Carico (la sezione più importante)

Prepara la patch più pesante che riesci e leggi **About / DSP load** dopo un
minuto di ogni passo. Annota `DSP`, `PEAK` e `OVER`.

1. Una bobina che suona, Gene-Size circa 0,3, Morph al massimo, Scatter 0,5.
2. Aggiungi delay e riverbero con mix 0,5.
3. Aggiungi HEADS: Mix 1, 4 testine, Wander, Rise, Reverse heads 100%, Wear
   Fold 50%.
4. Registra in una splice nuova mentre tutto questo suona.
5. Salva la bobina mentre tutto questo suona.

- [ ] `OVER` resta a 0 in tutti e cinque i passi.
- [ ] Nessun click o buco udibile.

## 6. Heimat in una traccia (modulo per la catena)

- [ ] Heimat come generatore sonoro su ognuna delle quattro tracce, suonato
      dai pad e dal sequencer della Move.
- [ ] Due o più Heimat insieme, con altri moduli Schwung: nessun click.
- [ ] Salva un set, spegni e riaccendi la Move, riapri il set: ogni Heimat
      torna con il suo campione e le sue impostazioni.
- [ ] Se hai un set salvato con Heimat 0.6 o precedente: si apre, le vecchie
      impostazioni restano, i controlli nuovi sono al valore iniziale.

## 7. Editor nel browser

- [ ] Schwung Manager → *Remote UI*: Heimat Solo nella scheda **Tool**, Heimat
      in una traccia nella vista della traccia.
- [ ] Girando un knob nel browser si muove sulla Move e viceversa; la bobina,
      i geni e i livelli si muovono mentre suona (Solo).
- [ ] Sul telefono: tutto entra nello schermo, niente scorre di lato.

## 8. Uso prolungato e uscita

- [ ] Lascia Heimat Solo suonare per un'ora con una patch in movimento: nessun
      crash, `OVER` ancora a 0, la Move risponde.
- [ ] Shift + Back esce da Heimat Solo; riaprilo: tornano le impostazioni e
      l'ultima bobina caricata o salvata. Lo stesso dopo uno spegnimento. (Una
      registrazione non salvata non viene conservata: è previsto.)

## Cosa mettere in una segnalazione

- Versione di Heimat (About), di Schwung e del firmware della Move.
- Quale modulo (Solo o traccia) e quale sezione di questa scheda.
- Cosa hai fatto, cosa è successo, cosa ti aspettavi.
- I valori `DSP / PEAK / OVER` se c'entrano suono o tempi.
- Se riguarda le impostazioni: il file `/data/UserData/schwung/heimat/state.json`
  (l'ultima sessione di Heimat Solo). Se riguarda un campione: il WAV, se puoi
  condividerlo.
