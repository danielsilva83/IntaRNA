/*
 * CommandLineParsing.cpp
 *
 *  Created on: 18.06.2014
 *      Author: Mmann
 */

#include "CommandLineParsing.h"

#include <cmath>
#include <stdexcept>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>

#include "AccessibilityDisabled.h"
#include "AccessibilityVienna.h"

#include "EnergyBasePair.h"
#include "EnergyVienna.h"

#include "OutputHandlerText.h"


using namespace boost::program_options;


const int CommandLineParsing::parsingCodeNotSet = 9999;

////////////////////////////////////////////////////////////////////////////

CommandLineParsing::CommandLineParsing( std::ostream& logStream )
	:
	opts_query("Query"),
	opts_target("Target"),
	opts_seed("Seed"),
	opts_inter("Interaction"),
	opts_general("General"),
	opts_cmdline_all(),

	parsingCode(parsingCodeNotSet),
	logStream(logStream),

	queryArg(""),
	query(),
	qAcc("NF",'F'),
	qAccConstr(""),
	qIntLenMax( 0, 99999, 0),
	qIntLoopMax( 0, 100, 16),

	targetArg(""),
	target(),
	tAcc("NF",'F'),
	tAccConstr(""),
	tIntLenMax( 0, 99999, 0),
	tIntLoopMax( 0, 100, 16),

	seedMinBP(3,20,7),

	temperature(0,100,37),

	energy("BF",'F')

{
	////  SEQUENCE OPTIONS  ////////////////////////////////////

	opts_query.add_options()
		("query,q"
			, value<std::string>(&queryArg)
				->required()
				->notifier(boost::bind(&CommandLineParsing::validate_query,this,_1))
			, "either an RNA sequence or the stream/file from where to read the query sequences; use 'STDIN' or 'STDERR' to read from standard input or error stream")
		("qAcc"
			, value<char>(&(qAcc.val))
				->default_value(qAcc.def)
				->notifier(boost::bind(&CommandLineParsing::validate_qAcc,this,_1))
			, std::string("accessibility computation : 'N'o accessibility contributions, or 'F'ull accessibility computation").c_str())
		("qAccConstr"
			, value<std::string>(&(qAccConstr))
				->notifier(boost::bind(&CommandLineParsing::validate_qAccConstr,this,_1))
			, std::string("accessibility computation : structure constraint for each sequence position: '.' no constraint, 'x' unpaired. Note, unpaired positions are excluded from interaction prediction as well!").c_str())
		("qIntLenMax"
			, value<int>(&(qIntLenMax.val))
				->default_value(qIntLenMax.def)
				->notifier(boost::bind(&CommandLineParsing::validate_qIntLenMax,this,_1))
			, std::string("maximal window size to be considered for interaction (and thus accessible) within the query (arg in range ["+toString(qIntLenMax.min)+","+toString(qIntLenMax.max)+"]; 0 defaults to the full sequence length)").c_str())
		("qIntLoopMax"
			, value<int>(&(qIntLoopMax.val))
				->default_value(qIntLoopMax.def)
				->notifier(boost::bind(&CommandLineParsing::validate_qIntLoopMax,this,_1))
			, std::string("maximal internal loop size to be considered in interactions within the query (arg in range ["+toString(qIntLoopMax.min)+","+toString(qIntLoopMax.max)+"]; 0 enforces stackings only)").c_str())
		;

	opts_target.add_options()
		("target,t"
			, value<std::string>(&targetArg)
				->required()
				->notifier(boost::bind(&CommandLineParsing::validate_target,this,_1))
			, "either an RNA sequence or the stream/file from where to read the target sequences; use 'STDIN' or 'STDERR' to read from standard input or error stream")
		("tAcc"
			, value<char>(&(tAcc.val))
				->default_value(tAcc.def)
				->notifier(boost::bind(&CommandLineParsing::validate_tAcc,this,_1))
			, std::string("accessibility computation : 'N'o accessibility contributions, or 'F'ull accessibility computation").c_str())
		("tAccConstr"
			, value<std::string>(&(tAccConstr))
				->notifier(boost::bind(&CommandLineParsing::validate_tAccConstr,this,_1))
			, std::string("accessibility computation : structure constraint for each sequence position: '.' no constraint, 'x' unpaired. Note, unpaired positions are excluded from interaction prediction as well!").c_str())
		("tIntLenMax"
			, value<int>(&(tIntLenMax.val))
				->default_value(tIntLenMax.def)
				->notifier(boost::bind(&CommandLineParsing::validate_tIntLenMax,this,_1))
			, std::string("maximal window size to be considered for interaction (and thus accessible) within the target (arg in range ["+toString(tIntLenMax.min)+","+toString(tIntLenMax.max)+"]; 0 defaults to the full sequence length)").c_str())
		("tIntLoopMax"
			, value<int>(&(tIntLoopMax.val))
				->default_value(tIntLoopMax.def)
				->notifier(boost::bind(&CommandLineParsing::validate_tIntLoopMax,this,_1))
			, std::string("maximal internal loop size to be considered in interactions within the target (arg in range ["+toString(tIntLoopMax.min)+","+toString(tIntLoopMax.max)+"]; 0 enforces stackings only)").c_str())
		;

	////  SEED OPTIONS  ////////////////////////////////////

	opts_seed.add_options()
		("seedMinBP"
			, value<int>(&(seedMinBP.val))
				->default_value(seedMinBP.def)
				->notifier(boost::bind(&CommandLineParsing::validate_seedMinBP,this,_1))
			, std::string("minimal number of inter-molecular base pairs within the seed region (arg in range ["+toString(seedMinBP.min)+","+toString(seedMinBP.max)+"])").c_str())
		;

	////  INTERACTION/ENERGY OPTIONS  ////////////////////////

	opts_inter.add_options()
		("temperature"
			, value<T_type>(&(temperature.val))
				->default_value(temperature.def)
				->notifier(boost::bind(&CommandLineParsing::validate_temperature,this,_1))
			, std::string("temperature to define energy parameters (arg in range ["+toString(temperature.min)+","+toString(temperature.max)+"])").c_str())
		;

	// TODO parse energy function selection

	////  GENERAL OPTIONS  ////////////////////////////////////

	opts_general.add_options()
	    ("version,v", "print version")
	    ("help,h", "show the help page with all available parameters")
	    ;

	////  GENERAL OPTIONS  ////////////////////////////////////

	opts_cmdline_all.add(opts_query).add(opts_target).add(opts_seed).add(opts_inter).add(opts_general);


}

////////////////////////////////////////////////////////////////////////////

CommandLineParsing::~CommandLineParsing() {
}

////////////////////////////////////////////////////////////////////////////

int
CommandLineParsing::parse(int argc, char** argv)
{
	// setup default parsing code = all ok
	parsingCode = 1;

	variables_map vm;
	try {
		store( parse_command_line(argc, argv, opts_cmdline_all), vm);
	} catch (error& e) {
		logStream <<"\nError : " <<e.what() << " : run with '--help' for allowed arguments\n\n";
		parsingCode = -1;
	}

	// if parsing was successful, check for help request
	if (parsingCode > 0) {
		if (vm.count("help")) {
			logStream
				<<"\nIntaRNA predicts RNA-RNA interactions.\n"
				<<"\nThe following program arguments are supported:\n"
				<< opts_cmdline_all
				<< "\n";
			parsingCode = 0;
		}
		if (vm.count("version")) {
			logStream <<PACKAGE_STRING << "\n";
			parsingCode = 0;
		}
	}

	// if parsing was successful, continue with additional checks
	if (parsingCode > 0) {
		try {
			// run all notifier checks
			notify(vm);
		} catch (required_option& e) {
			logStream <<"\n mandatory option '"<<e.get_option_name() << "' not provided\n";
			parsingCode = std::min(-1,parsingCode);
		} catch (error& e) {
			logStream <<e.what() << "\n";
			parsingCode = std::min(-1,parsingCode);
		}
	}

	// if parsing was successful, continue with final parsing
	if (parsingCode > 0) {
		try {

			// parse the sequences
			parseSequences("query",queryArg,query);
			// parse the sequences
			parseSequences("target",targetArg,target);

			// check for minimal sequence length (>=seedMinBP)
			for( size_t i=0; i<query.size(); i++) {
				if (query.at(i).size() < getSeedMinBp()) {
					logStream <<"\n length of query sequence "<<(i+1) <<" is below minimal number of seed base pairs (seedMinBP="<<getSeedMinBp()<<")\n";
					parsingCode = std::min(-1,parsingCode);
				}
			}
			for( size_t i=0; i<target.size(); i++) {
				if (target.at(i).size() < getSeedMinBp()) {
					logStream <<"\n length of target sequence "<<(i+1) <<" is below minimal number of seed base pairs (seedMinBP="<<getSeedMinBp()<<")\n";
					parsingCode = std::min(-1,parsingCode);
				}
			}

			// check qAccConstr - query sequence compatibility
			if (vm.count("qAccConstr") > 0) {
				// only for single sequence input supported
				if (validateSequenceNumber("qAccConstr",query,1,1)) {
					// check length
					if (qAccConstr.size() != query.at(0).size()) {
						logStream <<"\n qAccConstr and query sequence differ in size\n";
						parsingCode = std::min(-1,parsingCode);
					}
				}
			}
			// check tAccConstr - target sequence compatibility
			if (vm.count("tAccConstr") > 0) {
				// only for single sequence input supported
				if (validateSequenceNumber("tAccConstr",target,1,1)) {
					// check length
					if (tAccConstr.size() != target.at(0).size()) {
						logStream <<"\n tAccConstr and target sequence differ in size\n";
						parsingCode = std::min(-1,parsingCode);
					}
				}
			}

		} catch (std::exception& e) {
			logStream <<e.what() << "\n";
			parsingCode = std::min(-1,parsingCode);
		}
	}



	// flush all content that was pushed to the log stream
	logStream.flush();
	// return validate_* dependent parsing code
	return parsingCode;
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_query(const std::string & value)
{
	validate_sequenceArgument("query",value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_qIntLenMax(const int & value)
{
	// forward check to general method
	validate_numberArgument("qIntLenMax", qIntLenMax, value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_qIntLoopMax(const int & value)
{
	// forward check to general method
	validate_numberArgument("qIntLoopMax", qIntLoopMax, value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_qAcc(const char & value)
{
	// forward check to general method
	validate_charArgument("qAcc", qAcc, value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_qAccConstr(const std::string & value)
{
	// forward check to general method
	validate_structureConstraintArgument("qAccConstr", value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_target(const std::string & value)
{
	validate_sequenceArgument("target",value);
}


////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_tIntLenMax(const int & value)
{
	// forward check to general method
	validate_numberArgument("tIntLenMax", tIntLenMax, value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_tIntLoopMax(const int & value)
{
	// forward check to general method
	validate_numberArgument("tIntLoopMax", tIntLoopMax, value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_tAcc(const char & value)
{
	// forward check to general method
	validate_charArgument("tAcc", tAcc, value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_tAccConstr(const std::string & value)
{
	// forward check to general method
	validate_structureConstraintArgument("tAccConstr", value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_seedMinBP(const int & value) {
	// forward check to general method
	validate_numberArgument("seedMinBP", seedMinBP, value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_temperature(const T_type & value) {
	// forward check to general method
	validate_numberArgument("temperature", temperature, value);
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_charArgument(const std::string & name, const CommandLineParsing::CharParameter& param, const char & value)
{
	// alphabet check
	if ( ! param.isInAlphabet(value) ) {
		logStream <<"\n "<<name<<" = " <<value <<" : has to be one of '" <<param.alphabet <<"'\n";
		parsingCode = std::min(-1,parsingCode);
	}
}

////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_sequenceArgument(const std::string & name, const std::string & value)
{
	if (value.compare("STDIN") != 0 && value.compare("STDERR") != 0) {

		// check if it is a sequence
		if ( RnaSequence::isValidSequence(value) ) {

			// do nothing, all fine

		} else { // it is NOT a sequence !!!!!!!!!!!!!!!!!!

			// check if file accessible
			if ( !boost::filesystem::exists( value ) )
			{
				logStream <<"\n "<<name<<" '"<<value <<"' : Can't find the file!\n";
				parsingCode = std::min(-1,parsingCode);
			}
			else
			// check if it is a file
			if ( !boost::filesystem::is_regular_file( value ) )
			{
				logStream <<"\n "<<name<<" '"<<value <<"' : Is no file!\n";
				parsingCode = std::min(-1,parsingCode);
			}
			// TODO no file support yet
			NOTIMPLEMENTED("\n "+name+" '"+value +"' : Currently only direct single sequence input supported.\n");
		}

	} else {
		// TODO no stream support yet
		NOTIMPLEMENTED("\n "+name+" '"+value +"' : Currently only direct single sequence input supported.\n");
	}

	// TODO : check for ambiguous nucleotides 'N' --> give warning that these are ignored for pairing in interactions
}


////////////////////////////////////////////////////////////////////////////

void CommandLineParsing::validate_structureConstraintArgument(const std::string & name, const std::string & value)
{
	// check if valid alphabet
	if (value.find_first_not_of(Accessibility::AccessibilityConstraintAlphabet) != std::string::npos) {
		logStream <<"\n "<<name<<" '"<<value <<"' : contains invalid characters!\n";
		parsingCode = std::min(-1,parsingCode);
	} else {
		// check for base pair balance / nestedness
		int bpStackLvl = 0;
		for (std::string::const_iterator c = value.begin(); c!=value.end(); ++c) {
			switch (*c) {
			case '(' : ++bpStackLvl; break;
			case ')' : --bpStackLvl; break;
			default: break;
			}
			if (bpStackLvl<0) {
				logStream <<"\n "<<name<<" '"<<value <<"' : unbalanced base pairs!\n";
				parsingCode = std::min(-1,parsingCode);
			}
		}

	}
}

////////////////////////////////////////////////////////////////////////////

const CommandLineParsing::RnaSequenceVec &
CommandLineParsing::
getQuerySequences() const
{
	checkIfParsed();
	return query;
}

////////////////////////////////////////////////////////////////////////////

const CommandLineParsing::RnaSequenceVec &
CommandLineParsing::
getTargetSequences() const
{
	checkIfParsed();
	return target;
}

////////////////////////////////////////////////////////////////////////////

Accessibility*
CommandLineParsing::
getQueryAccessibility( const size_t sequenceNumber ) const
{
	checkIfParsed();
	// input check
	if (sequenceNumber >= getQuerySequences().size()) {
		throw std::runtime_error("CommandLineParsing::getQueryAccessibility : sequence number "+toString(sequenceNumber)+" is out of range (<"+toString(getQuerySequences().size())+")");
	}
	const RnaSequence& seq = getQuerySequences().at(sequenceNumber);
	switch(qAcc.val) {
	case 'N' : return new AccessibilityDisabled( seq, qIntLenMax.val );
	case 'F' : return new AccessibilityVienna( seq, qIntLenMax.val, qAccConstr, temperature.val, &logStream );
	default :
		NOTIMPLEMENTED("CommandLineParsing::getQueryAccessibility : qAcc = '"+toString(qAcc.val)+"' is not supported");
	}
	return NULL;
}

////////////////////////////////////////////////////////////////////////////

Accessibility*
CommandLineParsing::
getTargetAccessibility( const size_t sequenceNumber ) const
{
	checkIfParsed();
	// input check
	if (sequenceNumber >= getTargetSequences().size()) {
		throw std::runtime_error("CommandLineParsing::getTargetAccessibility : sequence number "+toString(sequenceNumber)+" is out of range (<"+toString(getTargetSequences().size())+")");
	}
	const RnaSequence& seq = getTargetSequences().at(sequenceNumber);
	switch(tAcc.val) {
	case 'N' : return new AccessibilityDisabled( seq, tIntLenMax.val );
	case 'F' : return new AccessibilityVienna( seq, tIntLenMax.val, tAccConstr, temperature.val, &logStream );
	default :
		NOTIMPLEMENTED("CommandLineParsing::getTargetAccessibility : tAcc = '"+toString(tAcc.val)+"' is not supported");
	}
	return NULL;
}

////////////////////////////////////////////////////////////////////////////

Energy*
CommandLineParsing::
getEnergyHandler( const Accessibility& accQuery, const ReverseAccessibility& accTarget ) const
{
	checkIfParsed();
	switch( energy.val ) {
	case 'B' : return new EnergyBasePair( accQuery, accTarget, qIntLoopMax.val, tIntLoopMax.val );
	case 'F' : return new EnergyVienna( accQuery, accTarget, qIntLoopMax.val, tIntLoopMax.val, temperature.val );
	default :
		NOTIMPLEMENTED("CommandLineParsing::getEnergyHandler : energy = '"+toString(energy.val)+"' is not supported");
	}
	return NULL;

}

////////////////////////////////////////////////////////////////////////////

void
CommandLineParsing::
parseSequences(const std::string & paramName,
					const std::string& paramArg,
					RnaSequenceVec& sequences )
{
	// clear sequence container
	sequences.clear();

	if (paramArg.compare("STDIN") == 0 ||paramArg.compare("STDERR") == 0 ) {
		// TODO stream handling
		NOTIMPLEMENTED("CommandLineParsing::parseSequences : stream handling missing");
	} else
	if (RnaSequence::isValidSequence(paramArg)) {
		// direct sequence input
		sequences.push_back(RnaSequence(paramName,paramArg));
	} else
	{
		// TODO file handling
		NOTIMPLEMENTED("CommandLineParsing::parseSequences : file handling missing");
	}

	// holds current validation status to supress checks once a validation failed
	bool valid = true;

	// ensure at least one sequence was parsed
	valid = valid && validateSequenceNumber(paramName, sequences, 1, 9999);
	// validate alphabet
	valid = valid && validateSequenceAlphabet(paramName, sequences);

}


////////////////////////////////////////////////////////////////////////////

bool
CommandLineParsing::
validateSequenceNumber( const std::string& paramName,
				const RnaSequenceVec& sequences,
				const size_t min,
				const size_t max)
{
	// check sequence number boundaries
	if (sequences.size() < min) {
		logStream <<"\n "<<paramName<<" requires at least "<<min <<" sequences!\n";
		parsingCode = std::min(-1,parsingCode);
		return false;
	} else
	if (sequences.size() > max) {
		logStream <<"\n "<<paramName<<" allows at most "<<max <<" sequences!\n";
		parsingCode = std::min(-1,parsingCode);
		return false;
	}
	// boundaries are met
	return true;
}

////////////////////////////////////////////////////////////////////////////

bool
CommandLineParsing::
validateSequenceAlphabet( const std::string& paramName,
				const RnaSequenceVec& sequences)
{
	bool allValid = true;
	// check each sequence
	for (int i=0; i<sequences.size(); i++) {
		// check if valid
		if (! RnaSequence::isValidSequence(sequences.at(i).asString())) {
			logStream <<"\n sequence " <<i<<" for parameter "<<paramName<<" is not valid!\n";
			parsingCode = std::min(-1,parsingCode);
			allValid = false;
		}
		// warn about ambiguous positions
		if (sequences.at(i).isAmbiguous()) {
			logStream <<"\n sequence " <<i<<" for parameter "<<paramName<<" is ambiguous! 'N' positions are ignored for interaction prediction!\n";
		}
	}
	return allValid;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

int  CommandLineParsing::getSeedMinBp() const {
	checkIfParsed();
	// TODO check if obsolete when using getSeed()
	return seedMinBP.val;
}

////////////////////////////////////////////////////////////////////////////


