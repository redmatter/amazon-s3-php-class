#include "stdio.h"
#include "stdlib.h"
#include "string.h"

int utf8len(char *s)
{
	int i = 0;
	int iBefore = 0;
	int count = 0;

	while (s[i] > 0)
		ascii: i++;

	count += i - iBefore;
	while (s[i])
	{
		if (s[i] > 0)
		{
			iBefore = i;
			goto ascii;
		}
		else
			switch (0xF0 & s[i])
			{
			case 0xE0:
				i += 3;
				break;
			case 0xF0:
				i += 4;
				break;
			default:
				i += 2;
				break;
			}
		++count;
	}
	return count;
}

char *advance(char *s, size_t max_len)
{

	int i, prev_i;

	for (i = 0; i < max_len; )
	{
		prev_i = i;

		if (s[i] > 0)
			i++;
		else
		{
			switch(s[i] & 0xF0)
			{
			case 0xE0:
				i += 3;
				break;
			case 0xF0:
				i += 4;
				break;
			default:
				i += 2;
				break;
			}
		}
	}

	if (i > max_len)
		return s + prev_i;
	else
		return s + i;
}

char *random_text(char *buff, size_t max_len)
{
	static char * text =
		"Article 1.\n"
		"Section 1\n"
		"All legislative Powers herein granted shall be vested in a Congress of the\n"
		"United States, which shall consist of a Senate and House of Representatives.\n"
		"In Ainm na Tríonóide Ró-Naofa is tobar don uile údarás agus gur chuici, ós í is\n"
		"críoch dheireanach dúinn, is dírithe ní amháin gníomhartha daoine ach \n"
		"gníomhartha Stát,\n"
		"Ar mbeith dúinne, muintir na hÉireann, ag admháil go huiríseal a mhéid atáimid\n"
		"faoi chomaoin ag Íosa Críost, ár dTiarna Dia, a thug comhfhurtacht dár sinsir i\n"
		"ngach cruatan ina rabhadar ar feadh na gcéadta bliain,\n"
		"Agus ar mbeith dúinn ag cuimhneamh go buíoch ar a chalmacht a rinneadarsan\n"
		"troid gan staonadh chun an neamhspleáchas is dual dár Náisiún a bhaint amach,\n"
		"Agus ar mbeith dúinn á chur romhainn an mhaitheas phoiblí a chur ar aghaidh \n"
		"maille le Críonnacht agus le hIonracas agus le Carthanacht de réir mar is cuí,\n"
		"ionas go dtiocfaidh linn a uaisleacht agus a shaoirse a chur in áirithe do gach aon\n"
		"duine, saol ceart comhdhaonnach a bhunú, aiseag a haontachta a thabhairt dár \n"
		"dtír, agus comhcharadra a dhéanamh le náisiúin eile,\n"
		"Atáimid leis seo ag gabháil an Bhunreachta seo chugainn, agus á achtú agus á\n"
		"thíolacadh dúinn féin.\n"
		"Nězoud v ptuni trůtě opyglož tět, nip vůdiměň dryblkad v vočlu chlá drá umu vepočou.\n"
		"šlouti glůptadaň beslí baněpě škama nihbli o děvrob. Tě kloudlyfla a vezrčib. \n"
		"Tryšra i ďab těfym vek moj timlů zláh, ni človrá bichtět i flz trabvaz, s \n"
		"slohra i padi vihynině dlouň z přavou zrož mrůzrétě čemě tir, skepu z frošt soudreplínib\n"
		"chukla ochá němeprou. Tkoca čechléděžlu vlůšt, těcluni vlipty netabřa vlyž nidzky \n"
		"čletkej. Z uni nit. Tisk hloudi a cách. Hlíbli nih z gytich klyvvrydrou chlyd. \n"
		"Grechranyp krůbnib. Býst by ktohřa něř pipo zlis res. Ovlu gláklub frý onim nině zí\n"
		"půml vrýhpy v žlam diby slip, v drégrůlkůvě članěh brapřou niš bikřou. A žlobotiň \n"
		"skedřá žlounibřeb žryflůvlou sodé ticlo glovlysru tyň.\n"
		"Wszyscy ludzie rodzą się wolni i równi pod względem swej godności i swych praw. Są\n"
		"oni obdarzeni rozumem i sumieniem i powinni postępować wobec innych w duchu braterstwa.\n"
	;
	size_t text_len = utf8len(text);

	size_t len = rand() % (40 + (rand() % (max_len - 40)));
	char *p = advance(text, rand() % (text_len - len));
	char *pend = advance(p, len);
	memcpy(buff, p, pend-p);
	buff[pend-p] = 0;
	return buff;
}
