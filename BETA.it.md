# Heimat 0.15.0 — scheda di collaudo della beta

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
      `HEIMAT 0.15.0 BETA`. Tieni a mente questa schermata: serve per le misure
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
- [ ] **Trim Key Noise** (LOOP, KEYS) a 100 e 200 ms — **di nuovo, 0.15**:
      ora il taglio conta dal **rilascio** del tasto. Registra una splice nuova
      premendo Record / Sample e tenendolo un attimo: il clic della pressione e
      quello del rilascio non ci sono più, né all'inizio né alla fine. Lo
      stesso sovraincidendo una splice. Con Record Sync acceso non si taglia
      niente. Se 100 ms basta, annotalo: era il valore che non bastava.
- [ ] **TRIM** (Shift + Capture) — nella 0.14 "OK, non perfetto": scrivi
      nel report **che cosa** non era perfetto (il punto, lo zoom, l'ascolto,
      il taglio?). sposta inizio e fine, zoom, ritocco con la
      rotella; mentre ritagli senti solo quello che resta. Taglia con il click
      della rotella: nessun clic sul taglio, le splice dopo sono intatte. Undo:
      torna tutto. Back senza tagliare: non cambia niente. Prova anche su una
      bobina di 2 minuti: nessun buco nel suono mentre taglia. Tocca un Track
      mentre ritagli: il ritaglio si annulla e si apre la prima pagina di
      quel Track.
- [ ] **Pagina LOOP** (Track 3 due volte): Record Length 1 bar con il clock
      in movimento: la registrazione si ferma da sola dopo una battuta, la
      battuta si riempie sullo schermo. Con 2 e 4 battute, anche con Record
      Sync acceso.
- [ ] After Recording = Overdub: a fine battuta il loop suona e la
      registrazione continua sopra (OVERDUB sullo schermo); Record la ferma.
      Anche quando fermi tu una registrazione Free.
- [ ] La testina Vari-Speed non c'è più: la pagina LOOP ha KEYS al knob 3 e
      SRC (Record Source) al knob 6; sullo schermo KEY TRIM al posto di HEAD.

## 3. Geni e Micro

- [ ] Pagina PLAY: Gene-Size, Slide, Morph, Vari-Speed (anche all'indietro e
      fermo), Organize.
- [ ] Pagina MICRO: ogni inviluppo, Scatter, Spread, Width, Selective e
      Threshold.
- [ ] **Gene Reverse (GREV)** (non provato nella 0.14): al 50% si sentono geni in avanti e all'indietro
      mescolati; al 100% vanno tutti all'indietro, anche con Vari-Speed
      all'indietro (e allora suonano in avanti).
- [ ] **Spread Intervals (INTV)**: Spread a 12 e Octaves → tre ottave nette;
      Fifths, Major, Minor → accordi riconoscibili; Free → una sfocatura.
- [ ] **Pattern (MICRO 2)**: Scatter e Spread alzati, Pattern 4: la nuvola
      ripete una frase di quattro geni; un altro SEED, un'altra frase; un pad
      nuovo la fa ripartire dall'inizio; i riquadri sullo schermo seguono i
      geni. Off: di nuovo caso libero.

## 3a. Pagine per famiglia (nuovo, 0.15)

- [ ] Track 1: PLAY, ancora MICRO, ancora MICRO 2, ancora PLAY.
- [ ] Track 2: REEL, ancora SETUP 1, ancora SETUP 2, ancora REEL.
- [ ] Track 3: REC, ancora LOOP, ancora REC.
- [ ] Track 4: EQ, HEADS, HEADS 2, REVERB, REVERB 2, di nuovo EQ (niente
      MICRO né SETUP).
- [ ] Da un'altra famiglia, ogni Track apre sempre la sua prima pagina; si
      accende il Track della famiglia sullo schermo (su MICRO, Track 1).
- [ ] Shift + Track 1 (MOD), Shift + Track 3 (HEADS), Shift + Track 4
      (REVERB, ancora EQ) come prima.
- [ ] SETUP 2, knob 8: di nuovo Volume.

## 3b. Modulatori (LFO 1, LFO 2, Envelope — nuovo, 0.15)

- [ ] Shift + Track 1: MOD 1 (LFO 1 e LFO 2, quattro knob ciascuno), ancora
      MOD 2 (Envelope e MRST; i knob 6–8 non fanno niente); le tracce si
      muovono.
- [ ] **Da ricontrollare (0.14: la percentuale non si vedeva)**: su PLAY tieni
      premuto Track 1 e gira Gene-Size: LFO1 > GENE con una percentuale che
      resta visibile mentre giri, anche con il dito sul knob; Gene-Size
      respira, la sua cella ha una bandierina a sinistra. Lascia: la pagina
      non è cambiata. Tocca Track 2 da solo: si apre REEL come prima.
- [ ] LFO 1 e LFO 2 a velocità diverse sullo stesso controllo (per esempio
      Morph): un movimento che non si ripete subito.
- [ ] Forma RAND a 1/4 con il clock in movimento, su Morph o Vari-Speed:
      gradini a tempo; SMTH li trasforma in glissati.
- [ ] Track 2 tenuto: LFO 2. Track 3 tenuto: Envelope. Track 4 tenuto non è
      più un modulatore: apre EQ subito.
- [ ] Envelope dai pad su Scatter o Gene-Size, in AD, Hold e Loop.
- [ ] MOD 2, MRST acceso: quando parte il clock tutti e due gli LFO ripartono
      dall'inizio.
- [ ] Track 1 + Delete: i percorsi di LFO 1 spariscono. Al massimo sedici
      percorsi.
- [ ] Esci da Heimat e riaprilo: i percorsi ci sono ancora. Una sessione della
      0.14 con percorsi su Wave: ora sono su LFO 1 (quelli di Random e Follow
      non ci sono più).
- [ ] **Editor**: il pannello Modulators ha tre colonne; ogni LFO è disegnato
      come la sua forma (due cicli) con un cursore che scorre, e cambiando la
      forma cambia il disegno; l'Envelope mostra attacco e decadimento. I
      percorsi, i cursori, Add route e le crocette funzionano nei due sensi con
      la Move.
- [ ] Carico (sezione 5) con otto percorsi attivi: `OVER` resta a 0.

## 3c. Suonare tranquilli (0.14)

- [ ] Tieni Undo, gira qualche knob e suona i pad, lascia: torna tutto.
      Tocca Undo da solo: annulla come prima. Shift + Undo: TMP resta in alto
      finché non premi di nuovo Undo.
- [ ] **Da controllare per primo**: Undo, Delete e i Track reagiscono al
      rilascio (serve per tenerli premuti). Se tenendo Undo non succede
      niente, segnalalo.
- [ ] **Da ricontrollare (0.14: si cancellavano i marker)**: tieni Delete a
      lungo, aspetta, poi gira un knob: torna al valore iniziale e nessun
      marker cancellato; lo stesso con un tocco. Tieni Delete da solo 3
      secondi e lascia: non succede niente. Un tocco breve di Delete cancella
      ancora il marker sotto il cursore. Shift + Delete tenuto 2 s: bobina
      nuova.
- [ ] Tieni Track 1 e tocca senza girare un knob modulato: ONE WAY / BOTH
      WAYS; la tacca sulla barra si muove di conseguenza.
- [ ] PLAY knob 6: filtro DJ, morbido da tutti e due i lati, nessun clic
      passando dal centro.

## 3d. Patch e Init (nuovo, 0.15)

- [ ] Menu → Save patch, slot 1: salva. Cambia il suono, Menu → Load patch,
      slot 1: torna. La bobina, il tempo, SOS e il volume non cambiano.
- [ ] Save patch su uno slot pieno: chiede CLICK AGAIN: REPLACE; al secondo
      clic sostituisce.
- [ ] Load patch su uno slot vuoto: SLOT N IS EMPTY, non carica niente.
- [ ] Menu → Init patch: chiede conferma; al secondo clic tutto il suono torna
      all'inizio e i percorsi spariscono. Load patch → BEFORE INIT: torna il
      suono di prima.
- [ ] Esci, riapri, spegni e riaccendi: le patch ci sono ancora.
- [ ] Editor, pannello Play: slot, Save / Load / Init funzionano e gli slot
      pieni si vedono (senza "(empty)").
- [ ] Carica una patch mentre suona: nessun click o buco (annota `OVER`).

## 4. Effetti e HEADS

- [ ] Ogni effetto ha la sua pagina: Track 4 passa per EQ, HEADS, HEADS 2,
      REVERB, REVERB 2; Shift + Track 4 apre REVERB.
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
- [ ] **Rumore** (0.13; nella 0.14 "ogni tanto più rumore": se succede,
      annota quale effetto e quali valori): Depth del riverbero a destra e a sinistra con Speed
      lento e veloce, shimmer, testine con Warm / Fold / Fuzz a feedback alto:
      nessun fruscio o crepitio che non fosse nel suono; senza suonare,
      silenzio.
- [ ] **Input Gate** (LOOP, GATE): con SOS sotto 1 e il riverbero acceso, il
      fruscio del microfono non continua a risuonare tra una frase e l'altra;
      suonando non si taglia niente. Prova anche -70 e -50 dB.
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

- [ ] Installa la 0.15.0 sopra Heimat Solo: nel menu dei moduli di Schwung ora
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
