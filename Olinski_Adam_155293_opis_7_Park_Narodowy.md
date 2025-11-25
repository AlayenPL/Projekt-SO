# Symulacja zwiedzania parku narodowego

**Temat projektu:** Park narodowy - nr.7 
**Przedmiot:** Systemy Operacyjne

---

## 1. Informacje ogólne

* **Autor:** Oliński Adam   
* **Numer albumu:** NR_155923
* **Temat:** Park narodowy - nr.7
* **Repozytorium GitHub:** https://github.com/AlayenPL/Projekt-SO

---

## 2. Treść zadania

Park narodowy jest dostępny dla zwiedzających w godzinach od **Tp** do **Tk**. Turyści przychodzą do parku w losowych momentach czasu. W ciągu jednego dnia do parku może wejść maksymalnie **N** osób.

Wejście do parku odbywa się poprzez kasę biletową (**K**). Dzieci poniżej 7 roku życia wchodzą bezpłatnie. Istnieje grupa osób **VIP (posiadających legitymację PTTK)**, które mogą wejść do parku bezpłatnie oraz z pominięciem kolejki do kasy.

Zwiedzanie parku odbywa się w **M‑osobowych grupach** pod opieką przewodnika. Osoby VIP mogą zwiedzać park samodzielnie. Liczba przewodników wynosi **P**.

Kasa rejestruje wszystkie osoby wchodzące do parku (ID procesu/wątku) oraz osoby wychodzące. Informacje o wyjściach przekazywane są kasjerowi przez przewodników.

---

## 3. Trasy zwiedzania

Zwiedzanie parku odbywa się losowo jedną z dwóch tras:

* **Trasa 1:** K → A → B → C → K
* **Trasa 2:** K → C → B → A → K

Każdy odcinek trasy zajmuje losowy czas. Jeżeli w grupie znajdują się dzieci poniżej 12 roku życia, czas przejścia danego odcinka jest wydłużony o 50%.

---

## 4. Atrakcje parku

### 4.1. Most wiszący (A)

* Jednocześnie na moście może przebywać maksymalnie **X1 osób** (X1 < M).
* Most umożliwia ruch tylko w jednym kierunku w danym momencie.
* Przejście mostu zajmuje losowy czas.
* Na most jako pierwszy wchodzi przewodnik grupy (jeżeli nie ma ruchu w przeciwnym kierunku).
* Na moście mogą jednocześnie przebywać osoby z różnych grup poruszających się w tym samym kierunku.
* Po przejściu przewodnik czeka na wszystkich członków swojej grupy.
* Dzieci poniżej 15 roku życia wchodzą na most pod opieką osoby dorosłej.
* Osoby VIP przed wejściem na most czekają w kolejce jak pozostali zwiedzający.

---

### 4.2. Wieża widokowa (B)

* Jednocześnie na wieżę może wejść maksymalnie **X2 osób** (X2 < 2M).
* Wieża posiada dwie klatki schodowe: jedną do wejścia i jedną do zejścia.
* Wejście, podziwianie widoków oraz zejście zajmuje losowy czas.
* Przewodnik nie wchodzi na wieżę – czeka na dole.
* Po zejściu wszystkich członków grupy przewodnik prowadzi grupę dalej.
* Dzieci poniżej 15 roku życia wchodzą pod opieką osoby dorosłej.
* Dzieci do 5 roku życia oraz ich opiekunowie nie mogą wejść na wieżę.
* Osoby VIP wchodzą na wieżę z pominięciem kolejki.
* Na polecenie przewodnika (**sygnał1**) turyści z danej grupy natychmiast schodzą z wieży.

---

### 4.3. Prom (C)

* Prom kursuje w obie strony; czas przeprawy w jedną stronę wynosi **T**.
* Jednocześnie na prom może wejść maksymalnie **X3 osób** (X3 < 1.5M).
* Na prom jako pierwszy wchodzi przewodnik grupy, następnie członkowie grupy.
* Na promie mogą znajdować się osoby z różnych grup.
* Po zejściu z promu przewodnik czeka, aż wszyscy członkowie grupy zakończą przeprawę.
* Dzieci poniżej 15 roku życia wchodzą na prom pod opieką osoby dorosłej.
* Osoby VIP wchodzą na prom z pominięciem kolejki.

Na polecenie przewodnika (**sygnał2**) członkowie grupy natychmiast wracają do kasy i opuszczają park, pomijając pozostałe atrakcje.

---

## 5. Model współbieżności

W symulacji występują następujące typy procesów/wątków:

* **Kasjer** – obsługa wejścia i wyjścia z parku, kontrola limitu N, rejestr zdarzeń.
* **Przewodnik** – formowanie grup, prowadzenie tras, synchronizacja grupy, wysyłanie sygnałów.
* **Turysta** – pojedynczy zwiedzający (z atrybutami: wiek, VIP).

Synchronizacja realizowana jest przy użyciu mechanizmów współbieżności (np. semafory, muteksy, monitory), zapewniających:

* ograniczenia pojemności atrakcji,
* kontrolę kierunku ruchu na moście,
* obsługę kolejek i priorytetów VIP,
* synchronizację członków grupy z przewodnikiem.

---

## 6. Opis testów

### Test 1 – Limit wejść

Sprawdzenie, czy do parku nie zostanie wpuszczona liczba turystów większa niż N.

### Test 2 – Priorytet VIP

Weryfikacja, że osoby VIP omijają kolejkę do kasy, wieży widokowej i promu.

### Test 3 – Pojemność atrakcji

Sprawdzenie, czy nie zostają przekroczone limity X1, X2 oraz X3.

### Test 4 – Ruch jednokierunkowy na moście

Potwierdzenie, że w danym momencie na moście odbywa się ruch tylko w jednym kierunku.

### Test 5 – Sygnały przewodnika

Sprawdzenie poprawnej reakcji grupy na sygnał1 (zejście z wieży) oraz sygnał2 (powrót do kasy).

---

## 7. Raportowanie

Przebieg symulacji jest zapisywany do plików tekstowych i obejmuje m.in.:

* wejścia i wyjścia turystów z parku,
* tworzenie i przemieszczanie się grup,
* zdarzenia na atrakcjach,
* obsługę sygnałów przewodnika.

---

## 8. Uwagi końcowe

Projekt realizuje symulację systemu współbieżnego z wieloma procesami współdzielącymi zasoby, z uwzględnieniem ograniczeń pojemności, priorytetów oraz synchronizacji czasowej.
