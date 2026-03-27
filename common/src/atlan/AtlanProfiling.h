#pragma once

class AtlanProfiling {
	public:

	static size_t CurrentTime();
};

struct atlanstamp {

	friend class AtlanProfiling;

	private:
	size_t time = 0;
	const char* id = "Unnamed Timestamp";

	public:

	atlanstamp(const char* identifier) : 
		id  (identifier), 
		time(AtlanProfiling::CurrentTime()) 
	{}


	// Logs
	void log(bool update = false);


};