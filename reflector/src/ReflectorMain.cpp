#include "reflector.h"
#include "cleaner.h"

void ReflectIdlib() {
	// JSON To Header probably isn't necessary anymore now that Denuvo is gone.
	//idlibCleaning::JsonToHeader();
	idlibCleaning::Pass1();
	idlibCleaning::Pass2();
	idlibReflection::Generate();
}

int main() {
	ReflectIdlib();
	return 0;
}