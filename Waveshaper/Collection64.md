# SXE Waveshaper – Collection (64 Funktionen)

Table = **Collection** (Eintrag nach „Custom“, damit bestehende Patches ihre Tabelle behalten). Position 0…10 V durchläuft alle 64, zwischen Nachbarn wird linear gemorpht.

**Rollen:** A Intensität (Drive, Faltung, Stufen), B Lage und Form (Bias, Symmetrie, Knie), C zusätzliche Morphologie, D Mischung oder Komplexität. Bereiche stehen in der Formel als `A(lo,hi)`; 0 V (unbelegter Pin) ergibt die Bereichsmitte.

## Gemessene Eigenschaften (Test 28.11.2026, echter Parser und Oversampler)

- Alle 64 Formeln werden vom vorhandenen Parser kompiliert, mit expliziter Multiplikation.
- Jede Formel wurde über alle 5^n Kombinationen der verwendeten A–D-Pins (−5, −2,5, 0, +2,5, +5 V) mal 401 Eingangswerte ausgewertet: kein NaN/Inf, |y| ≤ 1,0000, y(0)=0.
- Audio-Rate-Test im Modul: Position langsam, A 1,1 kHz, B 2,9 kHz, C 313 Hz, D Sägezahn, bei Off/2x/4x/8x: Ausgang endlich. Spitzen bis 6,6 V (nominal 5 V), verursacht durch Filter- und DC-Blocker-Überschwinger sowie das Morphing; nie über der ±20-V-Grenze.
- Klangliche Unterscheidung: Oberton-Spektren (Grundton bis 16. Harmonische) bei 14 Einstellungen verglichen. Kleinster mittlerer Abstand eines Paares: 2,4 dB (Sat Tape Warm ↔ Sat Transformer). Glatte Sättigungskurven sind einander von Natur aus ähnlich; die Abstände der übrigen Paare liegen bei 3 dB und mehr.
- Oversampling-Empfehlung: Abstand des 2x- und 4x-Ergebnisses zum 8x-Ergebnis, 1337-Hz-Sinus bei 4,5 V, A–D bei 0 V und +4 V. 2x bei ≤ −50 dB, sonst 4x bei ≤ −40 dB, sonst 8x. Ergebnis: 42 × 2x, 13 × 4x, 9 × 8x.
- Nicht getestet: Hörtest, Kurven mit Eingangspegeln über ±5 V, Betrieb in SynthEdit selbst (die Tests laufen in der mitgelieferten Testumgebung).

## Einschränkungen

- Sprünge in der Kurve (Quantisierer, wrap): Digi-Funktionen und Exp Triple Morph; dort langsame A–D-Modulation oder 8x.
- Mit DC = ja entsteht bei symmetrischem Eingang ein Gleichanteil, DC Block eingeschaltet lassen.
- Position morpht per Überblendung der Ausgänge. A–D wirken auf beide Nachbarn gleichzeitig, deshalb gilt die Rollenkonvention.
- Die Collection ersetzt keine der fünf bestehenden Tabellen.

## Dist (1–8)

| Nr | Name | Formel | A–D | Charakter | DC | Oversampling (2x / 4x Abstand zu 8x) |
|---|---|---|---|---|---|---|
| 1 | Dist Soft Drive | `tanh(x*A(1,12))/tanh(A(1,12))` | A Drive | tanh-Sättigung, normiert auf y(1)=1; der neutrale Allrounder | nein | 2x (-85 / -100 dB) |
| 2 | Dist Shaped Drive | `tanh(spow(x,B(0.5,2))*A(1,10))/tanh(A(1,10))` | A Drive, B Exponent 0,5…2 | Eingang wird vor der Sättigung durch spow geformt: B<1 weicht die Mitte auf, B>1 macht sie stiller und die Spitzen härter | nein | 2x (-64 / -78 dB) |
| 3 | Dist Tilt Drive | `tanh(x*A(1,10))*(1+B(-0.5,0.5)*x)/(1+abs(B(-0.5,0.5)))` | A Drive, B Neigung ±0,5 | tanh mit amplitudenabhängiger Neigung (1+B·x): gerade Obertöne, leichter DC | ja | 2x (-91 / -100 dB) |
| 4 | Dist Dynamic Drive | `tanh(x*A(1,8)*(1+C(0,3)*abs(x)))/tanh(A(1,8)*(1+C(0,3)))` | A Drive, C Pegelabhängigkeit | Drive wächst mit dem Betrag von x; laute Spitzen werden stärker verzerrt als leise Anteile | nein | 2x (-67 / -87 dB) |
| 5 | Dist Crossover | `tanh(A(1,6)*sign(x)*max(abs(x)-B(0,0.5),0)/(1-B(0,0.5)))/tanh(A(1,6))` | A Drive, B Totzone 0…0,5 | Totzone um Null (Übernahmeverzerrung) vor der Sättigung, ohne Sprung | nein | 4x (-44 / -58 dB) |
| 6 | Dist Exponential Drive | `sign(x)*(1-exp(-abs(x)*A(1,10)))/(1-exp(-A(1,10)))` | A Drive | 1−exp-Kurve: weiche Mitte, rundes Knie, anders als tanh-artige Kurven | nein | 2x (-62 / -81 dB) |
| 7 | Dist Quintic Drive | `(15*hard(x*A(1,4))-10*hard(x*A(1,4))^3+3*hard(x*A(1,4))^5)/8` | A Drive | Polynom fünfter Ordnung mit flachem Ende: Clip ohne Knick, sehr obertonarm | nein | 2x (-83 / -99 dB) |
| 8 | Dist Cross-Mod Drive | `tanh(x*A(1,8)+C(0.5,4)*sin(x*B(1,6)*pi))/tanh(A(1,8)+C(0.5,4))` | A Drive, B Frequenz, C Tiefe | Sinus-Modulation im Argument der Sättigung; B und C verändern die Kurvenform selbst | nein | 4x (-23 / -44 dB) |

## Clip (9–16)

| Nr | Name | Formel | A–D | Charakter | DC | Oversampling (2x / 4x Abstand zu 8x) |
|---|---|---|---|---|---|---|
| 9 | Clip Threshold | `hard(x/A(0.1,1))` | A Schwelle 0,1…1 | harter Clip, Schwelle verschiebbar | nein | 2x (-62 / -75 dB) |
| 10 | Clip Soft Knee | `sign(x)*(min(abs(x),A(0.1,0.9))+(1-A(0.1,0.9))*tanh(max(abs(x)-A(0.1,0.9),0)/(1-A(0.1,0.9))*B(1,6)))` | A Schwelle, B Knieschärfe | linear bis zur Schwelle, darüber weicher tanh-Übergang | nein | 2x (-56 / -70 dB) |
| 11 | Clip Asymmetric | `(hard(x*A(1,6)+B(0,0.8))-hard(B(0,0.8)))/(1+hard(B(0,0.8)))` | A Drive, B Versatz | Clip mit verschobenem Fenster; DC | ja | 2x (-53 / -65 dB) |
| 12 | Clip Moving Window | `(clamp(x*A(1,4),B(-0.6,0.6)-C(0.2,1),B(-0.6,0.6)+C(0.2,1))-clamp(0,B(-0.6,0.6)-C(0.2,1),B(-0.6,0.6)+C(0.2,1)))/(C(0.2,1)+abs(B(-0.6,0.6)))` | A Drive, B Fensterlage, C Fensterbreite | Clip-Fenster wandert und verändert seine Breite; DC | ja | 2x (-54 / -66 dB) |
| 13 | Clip Two Slope | `hard(sign(x)*(min(abs(x),C(0.2,0.8))*A(1,4)+max(abs(x)-C(0.2,0.8),0)*B(0,1)*A(1,4)))` | A Gain, B Außensteigung, C Knie | zwei Steigungen mit Übergang bei C, danach harte Grenze | nein | 2x (-54 / -67 dB) |
| 14 | Clip Folded | `fold(hard(x*A(1,4))*B(1,3))` | A Drive, B Faltung | hart begrenzt, dann Spitzen gefaltet: Clip mit zusätzlicher Oberwellenstruktur | nein | 4x (-35 / -48 dB) |
| 15 | Clip Soft-Hard Morph | `lerp(tanh(x*A(1,8))/tanh(A(1,8)),hard(x*A(1,8)),D(0,1))` | A Drive, D Mischung weich→hart | Übergang tanh→hard innerhalb der Formel | nein | 2x (-51 / -62 dB) |
| 16 | Clip Sine | `sin(hard(x*A(1,3))*B(0.5,1)*pi/2)/sin(B(0.5,1)*pi/2)` | A Drive, B Sinusanteil | Sinus-Clip: rundes, geschlossenes Ende ohne Knick | nein | 2x (-67 / -80 dB) |

## Sat (17–24)

| Nr | Name | Formel | A–D | Charakter | DC | Oversampling (2x / 4x Abstand zu 8x) |
|---|---|---|---|---|---|---|
| 17 | Sat Analog | `x*((1+A(1,6)^B(1.5,6))/(1+abs(x*A(1,6))^B(1.5,6)))^(1/B(1.5,6))` | A Drive, B Kniehärte 1,5…6 | Rapp-Kurve: B stellt von weichem bis fast hartem Knie | nein | 2x (-84 / -99 dB) |
| 18 | Sat Triode | `if(x>0,B(0.6,1)*tanh(x*A(1,6)),tanh(x*A(1,6)*B(0.6,1)))` | A Drive, B Asymmetrie | positive Hälfte früher gesättigt als negative, ohne Knick bei 0; DC | ja | 2x (-90 / -99 dB) |
| 19 | Sat Tape Warm | `tanh(x*A(1,5))*(1-B(0,0.3)*x^2)` | A Drive, B Spitzen-Sag | tanh, Spitzen leicht zurückgenommen | nein | 2x (-91 / -100 dB) |
| 20 | Sat Transformer | `(tanh(x*A(1,6))+B(0,0.3)*cheb(3,hard(x)))/(1+B(0,0.3))` | A Drive, B 3. Harmonische | tanh plus Chebyshev T3: der „Kern“-Oberton | nein | 2x (-85 / -94 dB) |
| 21 | Sat Soft Cubic | `1.5*hard(x*A(1,2.5))-0.5*hard(x*A(1,2.5))^3` | A Drive | kubische Sättigung, wenig Obertöne, endet flach | nein | 2x (-80 / -97 dB) |
| 22 | Sat Warm Even | `(tanh(x*A(1,5))+B(0,0.3)*tanh(x*A(1,5))^2)/(1+B(0,0.3))` | A Drive, B gerader Anteil | tanh plus Quadratterm: gerade Obertöne; DC | ja | 2x (-90 / -99 dB) |
| 23 | Sat Multi-Stage | `tanh(B(1,5)*tanh(x*A(1,6)))/tanh(B(1,5)*tanh(A(1,6)))` | A Drive Stufe 1, B Drive Stufe 2 | zwei tanh-Stufen hintereinander | nein | 4x (-46 / -87 dB) |
| 24 | Sat Sag | `tanh(x*A(1,6)/(1+C(0,3)*x^2))` | A Drive, C Einbruch | Drive sackt bei hohem Pegel ein; nicht monoton, die Kurve schwingt bei großem C zurück | nein | 2x (-90 / -99 dB) |

## Fold (25–32)

| Nr | Name | Formel | A–D | Charakter | DC | Oversampling (2x / 4x Abstand zu 8x) |
|---|---|---|---|---|---|---|
| 25 | Fold Triangle | `fold(x*A(1,8))` | A Faltung 1…8 | klassischer Dreieck-Wavefolder | nein | 4x (-35 / -46 dB) |
| 26 | Fold Sine | `sin(x*A(1,8)*pi/2)` | A Faltung | Sinus-Folder: glatte Faltung, kaum Alias | nein | 2x (-73 / -82 dB) |
| 27 | Fold Multi | `fold(fold(x*A(1,6))*B(1,3))` | A Faltung, B zweite Faltung | zweifach gefaltet: dichte, komplexe Obertonstruktur | nein | 8x (-19 / -34 dB) |
| 28 | Fold Asymmetric | `fold(x*A(1,6)*(1+B(0,0.8)*x))` | A Faltung, B Asymmetrie | Faltung mit pegelabhängiger Verzerrung des Eingangs; DC | ja | 4x (-34 / -46 dB) |
| 29 | Fold Moving | `(fold(x*A(1,6)+B(-0.5,2.5))-fold(B(-0.5,2.5)))/(1+abs(fold(B(-0.5,2.5))))` | A Faltung, B Arbeitspunkt −0,5…2,5 | der Arbeitspunkt wandert durch die Falten; DC | ja | 4x (-39 / -52 dB) |
| 30 | Fold Ripple | `fold(x*A(1,5)+C(0,0.6)*sin(x*B(2,12)*pi))` | A Faltung, B Ripple-Frequenz, C Ripple-Tiefe | Faltung mit überlagerter Welligkeit | nein | 8x (-20 / -34 dB) |
| 31 | Fold Feedback | `fold(x*A(1,4)+B(0.2,0.9)*fold(x*A(1,4)+B(0.2,0.9)*fold(x*A(1,4))))` | A Faltung, B Rückkopplung | dreifach verschachtelte Faltung wie eine Rückkopplungsschleife | nein | 4x (-32 / -54 dB) |
| 32 | Fold Complex | `sin(pi/2*B(1,4)*fold(x*A(1,6)))` | A Faltung, B Sinus-Faltung | Dreieckfaltung, danach Sinusfaltung | nein | 8x (-22 / -36 dB) |

## Harm (33–40)

| Nr | Name | Formel | A–D | Charakter | DC | Oversampling (2x / 4x Abstand zu 8x) |
|---|---|---|---|---|---|---|
| 33 | Harm Even | `(x+A(0,0.5)*x^2+B(0,0.3)*x^4)/(1+A(0,0.5)+B(0,0.3))` | A 2. Ordnung, B 4. Ordnung | gerade Harmonische über Potenzterme; DC | ja | 2x (-84 / -93 dB) |
| 34 | Harm Odd | `(hard(x)+A(0,1)*cheb(3,hard(x))+B(0,1)*cheb(5,hard(x)))/(1+A(0,1)+B(0,1))` | A 3. Harmonische, B 5. Harmonische | ungerade Harmonische über Chebyshev T3/T5 | nein | 2x (-67 / -76 dB) |
| 35 | Harm Exciter | `(x+A(0,1)*abs(x)*sin(x*B(2,8)*pi))/(1+A(0,1))` | A Menge, B Frequenz | Obertöne wachsen mit dem Pegel, leise Anteile bleiben sauber | nein | 2x (-65 / -82 dB) |
| 36 | Harm Chebyshev Glide | `hard(x)*lerp(cheb(floor(A(1,8)),hard(x)),cheb(floor(A(1,8))+1,hard(x)),frac(A(1,8)))` | A Ordnung 1…8 (stufenlos) | gleitet zwischen Chebyshev-Ordnungen; gerade/ungerade Ordnungen wechseln | ja | 2x (-73 / -82 dB) |
| 37 | Harm Polynomial | `hard(x)*(1+B(0,0.5)*hard(x)+A(-0.8,0.8)*hard(x)^2)/(1+B(0,0.5)+abs(A(-0.8,0.8)))` | A Kubikanteil ±0,8, B Quadratanteil | freies Polynom dritter Ordnung; DC bei B>0 | ja | 2x (-83 / -92 dB) |
| 38 | Harm Dynamic | `x*cos(x^2*A(0,4)*pi/2)` | A Phasenhub | x·cos(A·x²): Obertöne nehmen mit dem Pegel zu | nein | 2x (-72 / -81 dB) |
| 39 | Harm Sweep | `sin(A(1,9)*asin(hard(x)))` | A Ordnung 1…9 (stufenlos) | sin(A·asin x): stufenlose Chebyshev-Verallgemeinerung; Modulation von A „sweept“ die harmonische Struktur | nein | 2x (-71 / -80 dB) |
| 40 | Harm Sine Stack | `(sin(x*B(1,4)*pi/2)+A(0,1)*sin(x*B(1,4)*pi*1.5)+C(0,1)*sin(x*B(1,4)*pi*2.5))/(1+A(0,1)+C(0,1))` | A, C Oberwellen 3/5, B Grundfrequenz | drei Sinusanteile als Fourier-Stapel | nein | 2x (-73 / -82 dB) |

## Digi (41–48)

| Nr | Name | Formel | A–D | Charakter | DC | Oversampling (2x / 4x Abstand zu 8x) |
|---|---|---|---|---|---|---|
| 41 | Digi Bit Reduction | `hard(quant(x,2^A(1,8)))` | A Bits 1…8 (2^A Stufen) | Quantisierung auf 2^A Stufen; Sprünge | nein | 4x (-40 / -46 dB) |
| 42 | Digi Soft Quantize | `hard(x-D(0,1)*sin(x*A(2,16)*2*pi)/(A(2,16)*2*pi))` | A Stufen 2…16, D Weichheit | Treppe, die mit D von Rampe bis fast flach wird | nein | 4x (-42 / -94 dB) |
| 43 | Digi Smooth Steps | `hard((floor(x*A(2,12))+0.5+0.5*tanh(B(2,8)*(frac(x*A(2,12))-0.5))/tanh(B(2,8)*0.5))/A(2,12))` | A Stufen, B Rundung | glatte Treppenstufen | nein | 4x (-37 / -62 dB) |
| 44 | Digi Digital Fold | `fold(quant(x*A(1,6),B(2,16)))` | A Drive, B Stufen | Quantisierung vor der Faltung | nein | 8x (-31 / -33 dB) |
| 45 | Digi Mid-Rise | `hard(sign(x)*(floor(abs(x)*A(2,16))+0.5)/A(2,16))` | A Stufen 2…16 | Quantisierer ohne Nullstufe; Sprung um Null | nein | 8x (-34 / -39 dB) |
| 46 | Digi mu-Law | `sign(x)*((1+B(10,255))^hard(quant(log(1+B(10,255)*abs(hard(x)))/log(1+B(10,255)),A(2,16)))-1)/B(10,255)` | A Stufen, B µ 10…255 | komprimierte Quantisierung: feine Stufen bei leisen Signalen | nein | 8x (-30 / -37 dB) |
| 47 | Digi Wrap-Fold Morph | `lerp(wrap(x*A(1,4)),fold(x*A(1,4)),D(0,1))` | A Drive, D wrap→fold | Überlauf (Sprung) blendet in Faltung | nein | 8x (-17 / -23 dB) |
| 48 | Digi Crushed Sat | `tanh(hard(quant(x,B(3,24)))*A(1,6))/tanh(A(1,6))` | A Drive, B Stufen 3…24 | Quantisierung, danach Sättigung | nein | 8x (-31 / -38 dB) |

## Asym (49–56)

| Nr | Name | Formel | A–D | Charakter | DC | Oversampling (2x / 4x Abstand zu 8x) |
|---|---|---|---|---|---|---|
| 49 | Asym Positive Bend | `if(x>0,tanh(x*A(1,6))/A(1,6),x)` | A Biegung | positive Hälfte gebogen, negative linear; DC | ja | 2x (-86 / -95 dB) |
| 50 | Asym Negative Bend | `if(x<0,x-A(0,1)*x^2,x)/(1+A(0,1))` | A Dehnung | negative Hälfte gedehnt; DC | ja | 2x (-84 / -93 dB) |
| 51 | Asym Half-Wave | `x*(1-A(0,1)*(0.5-0.5*tanh(x*B(4,40))))` | A Anteil, B Übergangsschärfe | Halbwelle, nach A stufenlos; DC | ja | 2x (-65 / -92 dB) |
| 52 | Asym Positive Sat | `(tanh(x*A(1,6)+B(0,0.8))-tanh(B(0,0.8)))/(1+tanh(B(0,0.8)))` | A Drive, B Bias | bias-getriebene Sättigung; DC | ja | 2x (-90 / -99 dB) |
| 53 | Asym Negative Sat | `(atan(x*A(1,6)+B(-0.8,0))-atan(B(-0.8,0)))/(pi/2+atan(abs(B(-0.8,0))))` | A Drive, B Bias | wie Positive Sat mit arctan und negativer Bias | ja | 2x (-89 / -98 dB) |
| 54 | Asym Moving Bias | `(tanh(x*A(1,6)+B(-0.8,0.8)*(1-x^2))-tanh(B(-0.8,0.8)))/(1+abs(tanh(B(-0.8,0.8))))` | A Drive, B Bias | Bias wirkt in der Mitte, nicht an den Spitzen | ja | 2x (-90 / -99 dB) |
| 55 | Asym Dynamic | `tanh(x*A(1,6)+B(0,1)*x^2)` | A Drive, B Quadratterm | Asymmetrie wächst mit dem Pegel | ja | 2x (-90 / -99 dB) |
| 56 | Asym Alternating | `tanh(x*A(1,6)*(1+B(0,0.7)*sin(x*C(1,6)*pi)))` | A Drive, B Tiefe, C Frequenz | Verstärkung schwankt entlang der Kurve; wechselnde Asymmetrie | ja | 2x (-57 / -98 dB) |

## Exp (57–64)

| Nr | Name | Formel | A–D | Charakter | DC | Oversampling (2x / 4x Abstand zu 8x) |
|---|---|---|---|---|---|---|
| 57 | Exp Logistic | `lerp(x,sign(x)*A(2.8,4)*(A(2.8,4)*abs(hard(x))*(1-abs(hard(x))))*(1-A(2.8,4)*abs(hard(x))*(1-abs(hard(x)))),D(0,1))` | A Wachstum 2,8…4, D Mischung | zweifache logistische Abbildung auf |x|; Mitte zu Chaos | nein | 2x (-55 / -74 dB) |
| 58 | Exp Feedback Loop | `tanh(x*A(1,6)+B(0.5,3.2)*tanh(x*A(1,6)+B(0.5,3.2)*tanh(x*A(1,6))))` | A Drive, B Rückkopplung 0,5…3,2 | dreifach verschachteltes tanh | nein | 4x (-30 / -46 dB) |
| 59 | Exp Interference | `(sin(x*A(1,6)*pi/2)+sin(x*A(1,6)*B(1,1.5)*pi/2))/2` | A Frequenz, B Verstimmung 1…1,5 | zwei leicht verstimmte Sinus: Schwebungsmuster | nein | 2x (-79 / -88 dB) |
| 60 | Exp Fractal Sum | `(sin(x*A(1,4)*pi/2)+B(0,1)*sin(x*A(1,4)*pi)/2+C(0,1)*sin(x*A(1,4)*pi*2)/4+D(0,1)*sin(x*A(1,4)*pi*4)/8)/(1+B(0,1)/2+C(0,1)/4+D(0,1)/8)` | A Frequenz, B/C/D Oktavanteile | Sinusoktaven übereinander | nein | 2x (-78 / -86 dB) |
| 61 | Exp Recursive Sine | `sin(C(0.3,1.8)*pi/2*sin(B(0.3,1.8)*pi/2*sin(x*A(0.5,2.5)*pi/2)))` | A Eingang, B/C Rekursionstiefe | verschachtelter Sinus | nein | 2x (-62 / -78 dB) |
| 62 | Exp Phase Warp | `sin((x+A(0,0.6)*sin(x*B(1,6)*pi))*C(0.5,2.5)*pi/2)` | A Tiefe, B Frequenz, C Gain | phasenverzerrter Sinus (FM auf der Kurve) | nein | 4x (-34 / -76 dB) |
| 63 | Exp Triple Morph | `lerp(lerp(tanh(x*A(1,6))/tanh(A(1,6)),fold(x*A(1,6)),clamp(D(0,2),0,1)),wrap(x*A(1,6)),clamp(D(0,2)-1,0,1))` | A Drive, D 0…2 | tanh→fold→wrap; wrap springt | nein | 8x (-14 / -22 dB) |
| 64 | Exp Quad Mixer | `(0.1*x+A(0,1)*tanh(x*3)/tanh(3)+B(0,1)*fold(x*3)+C(0,1)*sin(x*pi*1.5)+D(0,1)*cheb(3,hard(x)))/(0.1+A(0,1)+B(0,1)+C(0,1)+D(0,1))` | A tanh, B fold, C sin, D T3 | alle vier Pins sind Mischregler | nein | 2x (-51 / -63 dB) |
