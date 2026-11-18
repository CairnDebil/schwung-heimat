# Heimat 0.12.1 — scheda di collaudo della beta

Grazie per il collaudo. Heimat ha superato tutti i test offline, ma non ha
ancora passato ore su una Move vera: questa scheda serve a questo. Ogni sezione
richiede 5–15 minuti; fai quelle che puoi, nell'ordine che vuoi, e spunta quello
che funziona. Quando qualcosa non va, apri una segnalazione **Beta report** (il
modulo chiede tutto quello che serve, vedi in fondo).

Per tutto ciò che registra usa le cuffie o un cavo nell'ingresso di linea: con
gli altoparlanti interni il microfono sente gli altoparlanti.

## 0. Prima di cominciare

- [ ] Annota la versione del **firmware della Move** e quella di **Schwung**.
- [ ] Installa `heimat-solo-module.tar.gz` (Schwung Manager → *Modules* →
      modulo personalizzato, oppure `scripts/install.sh`) e riavvia la Move.
- [ ] Apri Heimat → **Menu** → **About / DSP load**: deve dire
      `HEIMAT 0.12.1 BETA`. Tieni a mente questa schermata: serve per le misure
      di carico.

**Come leggere About / DSP load.** `DSP` è la parte media di ogni blocco audio
che Heimat usa da solo, `PEAK` il blocco più pesante degli ultimi secondi,
`OVER` quanti blocchi hanno impiegato più del tempo reale da quando Heimat è
partito. Si misura solo Heimat, non il resto della Move. `OVER` sopra 0 vuol
dire che probabilmente c'è stato un click o un buco: segnalalo sempre, insieme
a cosa stava suonando.

## 1. Caricare e suonare

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
- [ ] **Trim Key Noise** (LOOP, KEYS) a 100 e 200 ms: registra una splice
      nuova premendo Record / Sample: il clic del tasto non c'è più, né
      all'inizio né alla fine. Lo stesso sovraincidendo una splice. Con Record
      Sync acceso non si taglia niente.
- [ ] **TRIM** (Shift + Capture): sposta inizio e fine, zoom, ritocco con la
      rotella; mentre ritagli senti solo quello che resta. Taglia con il click
      della rotella: nessun clic sul taglio, le splice dopo sono intatte. Undo:
      torna tutto. Back senza tagliare: non cambia niente. Prova anche su una
      bobina di 2 minuti: nessun buco nel suono mentre taglia.
- [ ] **Pagina LOOP** (Track 3 due volte): Record Length 1 bar con il clock
      in movimento: la registrazione si ferma da sola dopo una battuta, la
      battuta si riempie sullo schermo. Con 2 e 4 battute, anche con Record
      Sync acceso.
- [ ] After Recording = Overdub: a fine battuta il loop suona e la
      registrazione continua sopra (OVERDUB sullo schermo); Record la ferma.
      Anche quando fermi tu una registrazione Free.
- [ ] Overdub Head = Vari-Speed: registra sopra una splice a metà velocità,
      poi suona a velocità normale (un'ottava sopra); all'indietro; a nastro
      fermo non scrive niente. Steady si comporta come prima.

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
- [ ] **Pattern (MICRO 2)**: Scatter e Spread alzati, Pattern 4: la nuvola
      ripete una frase di quattro geni; un altro SEED, un'altra frase; un pad
      nuovo la fa ripartire dall'inizio; i riquadri sullo schermo seguono i
      geni. Off: di nuovo caso libero.

## 3b. Modulatori

- [ ] Shift + Track 1: MOD 1 (Wave, Random), ancora MOD 2 (Follow,
      Envelope); le tracce si muovono.
- [ ] Su PLAY tieni premuto Track 1 e gira Gene-Size: WAVE > GENE con una
      percentuale; Gene-Size respira, la sua cella ha una bandierina a
      sinistra. Lascia: la pagina non è cambiata. Tocca Track 2 da solo: si
      apre REEL come prima.
- [ ] Random a 1/4 con il clock in movimento, su Morph o Vari-Speed: gradini
      a tempo; Smooth li trasforma in glissati.
- [ ] Follow su Input verso il mix del riverbero: suonando nell'ingresso il
      riverbero si apre, il silenzio lo chiude.
- [ ] Envelope dai pad su Scatter o Gene-Size, in AD, Hold e Loop.
- [ ] Track 1 + Delete: i percorsi di Wave spariscono. Al massimo sedici
      percorsi.
- [ ] Esci da Heimat e riaprilo: i percorsi ci sono ancora.
- [ ] Editor: il pannello Modulators mostra i percorsi, i cursori cambiano la
      profondità, Add route e le crocette funzionano nei due sensi con la Move.
- [ ] Carico (sezione 5) con otto percorsi attivi: `OVER` resta a 0.

## 4. Effetti e HEADS

- [ ] Ogni effetto ha la sua pagina: Track 4 passa per MICRO, MICRO 2, EQ,
      HEADS, HEADS 2, REVERB, REVERB 2, SETUP 1, SETUP 2; Shift + Track 4 apre REVERB.
- [ ] Encoder: uno scatto lento muove di poco, un giro veloce spazza tutto il
      campo; le scelte (inviluppo, routing, on/off) avanzano di un passo ogni due
      scatti e non saltano mai.
- [ ] Gene-Size: la prima metà del knob per i geni lunghi, la seconda per i
      grani; deve sembrare uniforme da un capo all'altro.
- [ ] REVERB: gira Size da una scatola a un cielo mentre suona (una piegatura
      dell'intonazione durante il giro è prevista); Pre-Delay con e senza
      Reverse; Decay in cima tiene per sempre; Absorb da denso a scuro; Depth a
      sinistra (chorus), a destra (stanza che vaga), tutto a destra (shimmer),
      con Speed lento e veloce; Tilt scuro e brillante.
- [ ] EQ e riverbero al massimo, Decay infinito, Depth su shimmer, per un
      minuto: forte, magari selvaggio, ma niente che esplode e niente silenzio
      dopo.
- [ ] HEADS: ogni Routing e Mode, ogni Wear al feedback massimo; Freeze tiene
      il nastro; *Clear heads* le zittisce subito.
- [ ] Le testine seguono il tempo: cambia il BPM interno e il tempo della Move.

## 5. Carico (la sezione più importante)

Prepara la patch più pesante che riesci e leggi **About / DSP load** dopo un
minuto di ogni passo. Annota `DSP`, `PEAK` e `OVER`.

1. Una bobina che suona, Gene-Size circa 0,3, Morph al massimo, Scatter 0,5.
2. Aggiungi il riverbero: mix 0,5, Size 0,9, Decay 0,8, Depth tutto a destra.
3. Aggiungi HEADS: Mix 1, 4 testine, Wander, Rise, Reverse heads 100%, Wear
   Fold 50%.
4. Registra in una splice nuova mentre tutto questo suona.
5. Salva la bobina mentre tutto questo suona.

- [ ] `OVER` resta a 0 in tutti e cinque i passi.
- [ ] Nessun click o buco udibile.

## 6. Aggiornare da Heimat Solo

- [ ] Installa la 0.12.1 sopra Heimat Solo: nel menu dei moduli di Schwung ora
      c'è **Heimat**, una volta sola, e Heimat Solo non c'è più.
- [ ] Aprilo: ritrovi le impostazioni e l'ultima bobina di prima
      dell'aggiornamento; le bobine salvate sono ancora in `Samples/Heimat`.
- [ ] Se avevi installato il vecchio Heimat per le tracce: resta tra i
      generatori sonori finché non lo togli, e non dà fastidio.

## 7. Editor nel browser

- [ ] Schwung Manager → *Remote UI*: Heimat nella scheda **Tool** mentre è
      aperto.
- [ ] Girando un knob nel browser si muove sulla Move e viceversa; la bobina,
      i geni e i livelli si muovono mentre suona.
- [ ] Sul telefono: tutto entra nello schermo, niente scorre di lato.

## 8. Uso prolungato e uscita

- [ ] Lascia Heimat suonare per un'ora con una patch in movimento: nessun
      crash, `OVER` ancora a 0, la Move risponde.
- [ ] Shift + Back esce da Heimat; riaprilo: tornano le impostazioni e
      l'ultima bobina caricata o salvata. Lo stesso dopo uno spegnimento. (Una
      registrazione non salvata non viene conservata: è previsto.)

## Cosa mettere in una segnalazione

- Versione di Heimat (About), di Schwung e del firmware della Move.
- Quale sezione di questa scheda.
- Cosa hai fatto, cosa è successo, cosa ti aspettavi.
- I valori `DSP / PEAK / OVER` se c'entrano suono o tempi.
- Se riguarda le impostazioni: il file `/data/UserData/schwung/heimat/state.json`
  (l'ultima sessione di Heimat). Se riguarda un campione: il WAV, se puoi
  condividerlo.
