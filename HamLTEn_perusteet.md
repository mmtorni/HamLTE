HamLTE-projektin alkuoletukset
==============================

Yksinkertaisen tukiaseman toteutus. Tavoitteena että LimeSDR puhuu USB-portistaan suoraan IP-paketteja. Tämä tarkoittaa että LTE-pino pitää toteuttaa LimeSDR:n FPGA:lla ja ARM-prosessorilla. Periaatteena on että kaupallisten päätelaitteiden pitää pystyä ottamaan yhteys tehtyyn HamLTE-tukiasemaan. Tämä sallii tukiaseman teossa merkittäviä yksinkertaistuksia. Voimme valita tuotettavaksi pelkän QPSK-modulaation, emme tarvitse tukea monelle tukiasemalle ja tukiasema muutenkin voi päättää laajalti päätelaitteiden käyttämät lähetysmuodot. Tämä yksinkertaistaa myös vastaanottoa reilusti. Tavoitteena on samalla määritellä yksinkertainen tukiasema johon on helppo tehdä HamLTE-päätelaite jonka ei tarvitse tukea koko LTE-standardia. Mallia voi ottaa NarrowBand-LTE -kehityksestä.

Yksinkertaistetun standardin rationalisointi
============================================

LTE-standardia on mahdollista yksinkertaistaa reilusti, koska iso osa LTE-standardista liittyy radioverkkojen tehokkaaseen käyttöön. Harrastuskäytössä tällainen maksimointi ei ole usein mielekästä. Lisäksi radioamatöörikäytössä tukiasemia on harvassa, joten pidempiä yhteyksiä usein kaivataan. Pitkä etäisyys usein rajoittaa modulaation QPSK:lle.

LTE-TDD:n perusteet. Peruskello 30.72MHz kun kaistanleveys on 20MHz. 5MHz:n kaistalla siis käytetään neljäsosaa tästä. 1.4MHz:n kaistalla kuudestoistaosaa, eli 192kHz. Kapein kaista on kiva koska sitä voi vastaanottaa myös RTL-SDR:llä.

Tukiasema ja päätelaitteet lähettävät vuorotellen. Tukiasema jakaa pääosin lähetysvuorot päätelaitteille. Lisäksi tukiasema osoittaa kanavat joilla voi lähettää ilman ennakkoilmoitusta (kätevä energiansäästötila).

Tukiaseman lähetteen tuottaminen
================================

Ethernet -> USB -> IP -> kanavien multipleksointi -> binäärivirta -> kompleksisymbolit -> IFFT -> DAC -> RF -> vahvistin -> antenni

Tukiaseman vastaanotto
======================

Ethernet <- USB <- IP <- kanavien demultipleksointi <- binäärivirta <- kompleksisymbolit <- normalisointi <- FFT <- synkronointi <- ADC <- RF <- vahvistin <- antenni

Kanavien multipleksointi
========================
Ensin allokoidaan pilottisignaalit ja kiinteät lähetteet.
Joillain kanavilla on muuttuva määrä dataa, joka on kaikki pakko lähettää modulaatiosta riippumatta.
Nämä kanavat allokoidaan seuraavaksi.
Sitten pitää jakaa päätelaitteille lähetysohjeet. Nämä allokoidaan seuraavaksi.
Jäljelle jääviin kohtiin voidaan sijoittaa käyttäjille menevää dataa eli PDSCH. Jokaiselle päätelaitteelle on potentiaalisesti erilainen modulaatio ja siten tarvitaan päätelaitekohtaiset pakettipuskurit.

Kanavien demultipleksointi
==========================
Tukiaseman lähete sisälsi päätelaitteille ohjeet datan sijoittelusta ja
modulaatioista. Nyt näitä käyttäen demoduloidaan binäärivirrasta
päätelaitekohtaiset lähetteet. Niitä ei ole pakko erotella toisistaan kuten lähetyksessä vaan ne voidaan vain
suoraan lähettää IP-paketteina eteenpäin.

LTE:n peruskäsitteet
====================

Kanava
  LTE jakaa lähetteen palasiin, joita se kutsuu kanaviksi. Kanavan määritelmään sisältyy aikaväli, modulaatio ja alikantoaalto

Alikantoaallot lähetetään rinnakkain radioaalloilla, mutta käytännössä ne ovat ennen IFFT:tä sarjamuotoisia. Täten digitaalista signaalia tuotettaessa ja tulkittaessa kanava tarkoittaa sijainteja sarjamuotoisessa binäärivirrassa.

Koska LTE tukee tällä hetkellä suurimmillaan 256-QAMia (harva päätelaite tai tukiasema 2017 alussa tukee yli 64-QAMia), voidaan binäärivirta esittää tavuina. Tällöin QPSK-modulaatiota käytettäessä ylimmät kaksi bittiä sisältävät varsinaisen signaalin ja alimmat kuusi bittiä ovat nollia. Jos käytetään tiheämpää modulaatiota, käytetään enemmän bittejä tavun yläreunasta.

Tukiaseman vastaanottaessa signaalia päätelaitteilta, vastaanotetun signaalin tulkinta binäärivirraksi voi vaatia päätelaitekohtaista signaalin voimakkuuden normalisointia. Tukiasema pyrkii kuitenkin välttämään tätä lähettämällä päätelaitteelle ohjeen käytettävästä lähetystehosta.


LTE-TDD:n kanavat päätelaitteen lähetteessä
==========================================
Lähde: http://www.rfwireless-world.com/Tutorials/LTE-Bearer-types.html

PUCCH
-----
PUCCH  = UCI               : BPSK-QPSK
  sisältää: HARQ ACK/NACK, CQI, MIMO feedback=RI+PMI

PUSCH
-----
PUSCH  = RRC, UCI, data    : QPSK-256QAM + MIMO. Yleensä päätelaitteet eivät tue kuin QPSK ja 16QAM.
                           TODO QPSK pakollinen kun "TTI bundling käytössä"
mutta jos ei ole RRC:tä tai dataa käytetään
Päätelaite voi lähettää 16QAMilla jos se ei tue tiheämpää modulaatiota ja tukiasema pyytää sellaista.

PUCCH voi sisältää:
  1-2 HARQ-ACK
  1-2 SR eli Scheduling Request
  1-2 CQI

PUCCH  allokoidaan alikaantoaalloille reunilta alkaen että keskelle jäisi mahdollisimman suuri yhtenäinen alue PUSCH-lähetteille.


PRACH = Physical Random Access Channel
--------------------------------------
Erityinen esilähete jolla päätelaite ilmoittaa tarvitsevansa lähetysvuoron. Tässä käytetään 1.25kHz:n alikantoaaltojakoa toisin kuin muissa lähetteissä.
Kaistanleveys on 90kHz, eli 6 OFMDA-alikantoaaltoa. Tämä kanava käyttää kuitenkin 72x 1.25kHz:n alikantoaaltoja.
Tässä on neljä erilaista formaattia, jotka valitaan etäisyyden mukaan tukiasemasta 15/30/78/108km.
Tälle kanavalle varataan 1-3ms ja 6 RB leveä kohta.
Tukiasema päättää millaista formaattia käytetään.




LTE-TDD:n kanavat tukiaseman lähetteessä
==========================================

PBCH   = Physical Broadcast Channel. Lähettää MIBejä BCH-kuljettimella BCCH-loogisessa kanavassa.
PMCH   = 
PHICH  = 
PDSCH  =  
PDCCH  = 
PCFICH = 

Muita kanaviin liittyviä käsitteitä:
BCCH   = 

PBCH = Physical Broadcast Channel
---------------------------------
Sisältää ainoastaan MIB-lähetteen. Tietosisältö on korkeintaan 24-bittiä pitkä, varmistetaan CRC-16:lla, toistetaan 12 kertaa ja QPSK-moduloidaan.

MIB-tiedote lähetetään 10ms välein PBCH-kanavalla. Sisältää tukiaseman lähetteen kaistanleveyden ja radiokehyksen numeron. Radiokehyksen numero kasvaa yhdellä aina 10ms:n välein. MIB-tiedote pitää lähettää samana aina neljä kertaa peräkkäin, jonka jälkeen se voi muuttua.

SIB-lähetteet
=============
SIB-lähetteet ovat vaihtuvia lähetteitä, joista SIB1-2 ovat välttämättömiä. Esimerkkejä tiedoista mitä näissä on:
SIB1: MCC, MNC, TAC, SIB mapping
SIB2: Lähetys- ja vastaanotto-ohjeet päätelaitteelle
SIB3: Tukiaseman valintaohjeet samalla taajuuskaistalla
SIB4: Saman taajuuskaistan naapurisolujen tiedot sekä ohjeet toisiin tukiasemiin ja GSM/UMTS/LTE/WiFi -vaihdosta
SIB5: Eri taajuuskaistalla LTE-naapurisolujen tiedot
SIB6: WCDMA-naapurisolut
SIB7: EDGE/GERAN-naapurisolut
SIB8: CDMA-2000- ja EVDO-naapurisolut
SIB9: Tukiaseman nimi (1-48 tavua) (käytössä femtosoluissa tai pienissä soluissa)
SIB10-11: Luonnonkatastrofivaroitukset (ETWS-järjestelmä)
SIB14: Ohjeet IoT-päätelaitteiden yhteydenottojen määrän rajoittamisesta (Extended Access Barring)
SIB16: Kellonaika
SIB17: WLAN offload



PMCH = Physical Multicast Channel
---------------------------------
1-3 OFDM symbolia subframen alussa. Käytetään mitä tahansa modulaatiota. Lähetteitä jotka pitää levittää kaikkialle tukiaseman kuuluvuusalueelle. Tässä voisi lähettää esimerkiksi TV- tai radiolähetettä joka kiinnostaa useita kuulijoita.

PHICH = Physical Hybrid ARQ Indicator Channel
---------------------------------------------
ACK/NACK -palautteet PUSCH-kanavalla lähetetystä datasta. Lähetetään aina BPSK-modulaatiolla.

Sisältää 0-1 palautetta per subframe per päätelaite. Eli yksi per TB.


PDSCH = Physical Downlink Shared Channel
----------------------------------------
Jos ei mitään lähetettävää, lähetetään sen sijaan PDCCH joka sisältää vain PHY-kontrollitiedon.
SIB-lähetteet, paging, RRC ja tärkeimpänä käyttäjien data.


PCFICH = 
---------------------------

PB


MIB ja SIB -lähetteet
=====================
MIB-lähete
----------
Lähetetään 40ms välein PBCH-kanavalla. Muuttumaton osa joka kuvaa tukiaseman käyttämää lähetettä.

SIB-lähetteet
-------------
SIB1 lähetetään neljänä kopiona 20ms välein. Se voi siten muuttua 80ms välein.




OFDM-modulointi
===============

Päätetään symbolien pituus ja syklisen prefiksin mitta. Syklinen prefiksi helpottaa signaalin perillemenoa heijastuksien eli monitie-etenemisen tapauksessa. Syklinen prefiksi käytännössä tarkoittaa että lähetetään symbolia pidempään kuin aivan minimaalinen määrä ja aloitetaan toistuvan aaltomuodon aloittaminen hieman ennen nimellistä alkukohtaa. Eli analogiana ei aloiteta siniaallon lähettämistä nollakohdasta, vaan hieman taaempaa. Syklisen prefiksin pituus kertoo kuinka paljon taaempaa.

Helpoimmillaan tuetaan vain QPSK-modulaatiota. Tämä on laskennallisesti helpoin vastaanottaa hyvän signaali-kohinasuhteen aikana.

PHY-lyhenteitä
==============
Aggregation = 1,2,4 tai 8 peräkkäistä CCE:tä = 9-72 REGiä = 36-288 RE:tä
1 CCE = 9 peräkkäistä REG:iä = 36 peräkkäistä RE:tä
1 REG = 4 peräkkäistä RE:tä kanavavirrassa. Ne eivät välttämättä ole peräkkäisiä radiolähetteessä koska väliin voi tulla pilotteja
1 RE = yksi alikantoaallon symboli, eli 2-8 bittiä jos QPSK -256-QAM modulaatioilla
1 RE = yksi alikantoaalto yhden OFDM-symbolin aikana. Sisältää 2-64 bittiä informaatiota riippuen modulaatiosta ja MIMOsta. Enimmillään 64 bittiä jos käytössä on 256-QAM ja 8x8 SU-MIMO. 2 bittiä jos QPSK ilman MIMOa.
1 RB = kaksitoista alikantoaaltoa 6-7 peräkkäisen symbolin aikana. Sisältää siten 12x6-7=72-84 RE:tä. Peräkkäisten symbolien lukumäärä riippuu syklisen prefiksin pituudesta. 180kHz leveä ja 0.5ms pitkä.



PTS = pilottisignaali eli kiinteä lähete jota voi käyttää synkronointiin. Niiden sijainti lähetteessä sisältää pienen määrän informaatiota. Tähän informaatioon koodataan tukiaseman solutunniste ja muuta pientä tietoa jota pitää lähettää usein.

PCFICH, PHICH sisältävät tukiaseman kontrollilähetteen ymmärtämiseksi tarvittavaa tietoa



Linkkejä:
 * Kuinka päätelaite tulkitsee ja löytää kontrolli-informaation lähetteestä
    http://nitintayal-lte-tutorials.blogspot.fi/2013/05/all-about-pdcch-and-cce-allocation.html
 * LTE-signaaliin sisältyvät kanavat eli aikaväli-modulaatio-alikantoaalto-määritelmät graafisesti:
    http://niviuk.free.fr/lte_resource_grid.html?duplex_mode=tdd
 * Mikä on S1-rajapinta ja miksi sitä ei tarvita HamLTE:ssä
    http://www.rfwireless-world.com/Tutorials/LTE-Bearer-types.html
 * Päätelaitteen aikasynkronointiprosessi
    http://www.sharetechnote.com/html/BasicProcedures_LTE.html#TimeSyncProcess
 * Päätelaitteen tukiaseman valintaprosessi
    http://www.sharetechnote.com/html/BasicProcedures_LTE.html#CellSelection

LTE-OFDM aikatasossa
====================
Yhden millisekunnin aikana lähetetään 12 tai 14 symbolia riippuen Cyclic Prefixin pituudesta.

Jokaisen millisekunnin alussa ensimmäinen symbolia kertovat tiedot modulaatioista ja tulkinnasta myöhemmin saman millisekunnin sisällä lähetettäville symboleille. Samaan symboliin tukiasema lähettää ACK/NACK-tiedon päätelaitteen lähetteistä. Tarvittaessa lähetetään lisäksi 1-3 symbolia joissa lisää tietoja modulaatioista ja tulkinnasta.

TDD:ssä 10ms:n aikana tukiasema lähettää 1-2 kertaa ja päätelaite 1-2 kertaa.

Edellä mainitun lisäksi lähetetään referenssi- ja synkronointisignaaleja. Niiden sijainnit ovat vakiot.

PSS-signaali on yhden symbolin pituinen ja käyttää koko tukiaseman kaistanleveyden. Se lähetetään 5ms:n välein.
SSS-signaali on yhden symbolin pituinen ja käyttää aina 1.4MHz:n leveyden. Se lähetetään 5ms:n välein.

MBSFN-kanava on erikoinen. Se lähetetään aina viimeisellä millisekunnilla ja se on suunniteltu lähetettäväksi samanaikaisesti kaikilta tukiasemilta samalla taajuudella ja modulaatiolla.

LTE:ssä kaikki toistuu alimmalla tasolla 1ms:n välein, joskin jotkut signaalit lähetetään eri toistolla.
Ensiksi lähetetään

LTE-OFDM vs muut OFDM:t
=======================
Erikoisin ominaisuus LTE:n OFDM:ssä on että alikantoaaltojen modulaatiot eivät ole kiinteitä vaan valitaan vastaanottajakohtaisesti.
Tämän voi nähdä johtuvan siitä että LTE:n OFDM:ssä jokaisella symbolilla voi olla useita vastaanottajia erilaisissa olosuhteissa.

Wi-Fi:n OFDM:ssä aina lähetetään kahden pisteen eli päätelaitteen ja tukiaseman välillä. DVB-T ja DAB-T:ssä taasen lähetetään vain yksi yhteinen signaali kaikille vastaanottajille.

Olisi yksinkertaisempaa käyttää aina samaa modulaatiota, mutta silloin lähetettäisiin tarpeettoman hitaasti hyvissä olosuhteissa oleville vastaanottajille.

LTE:n OFDM:ssä on erikoista että synkronointisignaalit lähetetään vakioaikavälein eikä aina lähetteen alussa.
