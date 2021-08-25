/* ESP_QRcode. multidisplay version
 * Depending the libraries you have installed/you plan to use uncomment the 
 * corresponding defines previous to compilation
 */

#include <GxDEPG0213BN/GxDEPG0213BN.h> 

class QRcode
{
	private:
		GxDEPG0213BN *eink;
		void render(int x, int y, int color);

	public:
		QRcode(GxDEPG0213BN *display);

		void init();
		void create(String message);
		
};
