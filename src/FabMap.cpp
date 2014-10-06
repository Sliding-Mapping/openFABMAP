/*------------------------------------------------------------------------
 Copyright 2012 Arren Glover [aj.glover@qut.edu.au]
                Will Maddern [w.maddern@qut.edu.au]

 This file is part of OpenFABMAP. http://code.google.com/p/openfabmap/

 OpenFABMAP is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the Free Software
 Foundation, either version 3 of the License, or (at your option) any later
 version.

 OpenFABMAP is distributed in the hope that it will be useful, but WITHOUT ANY
 WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 details.

 For published work which uses all or part of OpenFABMAP, please cite:
 http://ieeexplore.ieee.org/xpl/articleDetails.jsp?arnumber=6224843

 Original Algorithm by Mark Cummins and Paul Newman:
 http://ijr.sagepub.com/content/27/6/647.short
 http://ieeexplore.ieee.org/xpl/articleDetails.jsp?arnumber=5613942
 http://ijr.sagepub.com/content/30/9/1100.abstract

 You should have received a copy of the GNU General Public License along with
 OpenFABMAP. If not, see http://www.gnu.org/licenses/.
------------------------------------------------------------------------*/

// git test
#include "../include/openfabmap.hpp"

using std::vector;
using std::list;
using std::map;
using std::multiset;
using std::valarray;
using cv::Mat;

/*
	Calculate the sum of two log likelihoods
*/
// a = log( P1 )
// b = log( P2 )
// return value = log( exp(a) + exp(b) ) = log( P1 + P2 )
double logsumexp(double a, double b) {
	return a > b ? log(1 + exp(b - a)) + a : log(1 + exp(a - b)) + b;
}

namespace of2 {

FabMap::FabMap(const Mat& _clTree, double _PzGe,
		double _PzGNe, int _flags, int _numSamples) :
	clTree(_clTree), PzGe(_PzGe), PzGNe(_PzGNe), flags(
			_flags), numSamples(_numSamples) {
	
	CV_Assert(flags & MEAN_FIELD || flags & SAMPLED);
	CV_Assert(flags & NAIVE_BAYES || flags & CHOW_LIU);
	if (flags & NAIVE_BAYES) {
		PzGL = &FabMap::PzqGL;
	} else {
		PzGL = &FabMap::PzqGzpqL;
	}

	//check for a valid Chow-Liu tree
	CV_Assert(clTree.type() == CV_64FC1);
	cv::checkRange(clTree.row(0), false, NULL, 0, clTree.cols);
	cv::checkRange(clTree.row(1), false, NULL, DBL_MIN, 1);
	cv::checkRange(clTree.row(2), false, NULL, DBL_MIN, 1);
	cv::checkRange(clTree.row(3), false, NULL, DBL_MIN, 1);

	// TODO: Add default values for member variables
	Pnew = 0.9;
	sFactor = 0.99;
	mBias = 0.5;
}

FabMap::~FabMap() {
}

const std::vector<cv::Mat>& FabMap::getTrainingImgDescriptors() const {
	return trainingImgDescriptors;
}

const std::vector<cv::Mat>& FabMap::getTestImgDescriptors() const {
	return testImgDescriptors;
}

// addTraining is used to add image descriptors to
// trainingImgDescriptors which is a collection of 
// image descriptors
// difference between the two addTraining functions below is:
// the input arguments are different ...
// seems a strategy for more flexible input arguments
void FabMap::addTraining(const Mat& queryImgDescriptor) {
	CV_Assert(!queryImgDescriptor.empty());
	vector<Mat> queryImgDescriptors;
	for (int i = 0; i < queryImgDescriptor.rows; i++) {
		queryImgDescriptors.push_back(queryImgDescriptor.row(i));
	}
	addTraining(queryImgDescriptors);
}

void FabMap::addTraining(const vector<Mat>& queryImgDescriptors) {
	for (size_t i = 0; i < queryImgDescriptors.size(); i++) {
		CV_Assert(!queryImgDescriptors[i].empty());
		CV_Assert(queryImgDescriptors[i].rows == 1);
		CV_Assert(queryImgDescriptors[i].cols == clTree.cols);
		CV_Assert(queryImgDescriptors[i].type() == CV_32F);
		trainingImgDescriptors.push_back(queryImgDescriptors[i]);
	}
}

void FabMap::add(const cv::Mat& queryImgDescriptor) {
	CV_Assert(!queryImgDescriptor.empty());
	vector<Mat> queryImgDescriptors;
	for (int i = 0; i < queryImgDescriptor.rows; i++) {
		queryImgDescriptors.push_back(queryImgDescriptor.row(i));
	}
	add(queryImgDescriptors);
}

void FabMap::add(const std::vector<cv::Mat>& queryImgDescriptors) {
	for (size_t i = 0; i < queryImgDescriptors.size(); i++) {
		CV_Assert(!queryImgDescriptors[i].empty());
		CV_Assert(queryImgDescriptors[i].rows == 1);
		CV_Assert(queryImgDescriptors[i].cols == clTree.cols);
		CV_Assert(queryImgDescriptors[i].type() == CV_32F);
		testImgDescriptors.push_back(queryImgDescriptors[i]);
	}
}

void FabMap::compare(const Mat& queryImgDescriptor,
			vector<IMatch>& matches, bool addQuery,
			const Mat& mask) {
	CV_Assert(!queryImgDescriptor.empty());
	vector<Mat> queryImgDescriptors;
	// very weird
	// why copy queryImgDescriptor to 
	//          queryImgDescriptors
	// maybe the following called compare function would
	// change its variables, namely, queriImgDescriptors
	
	for (int i = 0; i < queryImgDescriptor.rows; i++) {
		queryImgDescriptors.push_back(queryImgDescriptor.row(i));
	}
	compare(queryImgDescriptors,matches,addQuery,mask);
}

void FabMap::compare(const Mat& queryImgDescriptor,
			const Mat& testImgDescriptor, vector<IMatch>& matches,
			const Mat& mask) {
	CV_Assert(!queryImgDescriptor.empty());
	vector<Mat> queryImgDescriptors;
	for (int i = 0; i < queryImgDescriptor.rows; i++) {
		queryImgDescriptors.push_back(queryImgDescriptor.row(i));
	}

	CV_Assert(!testImgDescriptor.empty());
	vector<Mat> testImgDescriptors;
	for (int i = 0; i < testImgDescriptor.rows; i++) {
		testImgDescriptors.push_back(testImgDescriptor.row(i));
	}
	compare(queryImgDescriptors,testImgDescriptors,matches,mask);

}

void FabMap::compare(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors,
		vector<IMatch>& matches, const Mat& mask) {
	CV_Assert(!queryImgDescriptor.empty());
	vector<Mat> queryImgDescriptors;
	for (int i = 0; i < queryImgDescriptor.rows; i++) {
		queryImgDescriptors.push_back(queryImgDescriptor.row(i));
	}
	compare(queryImgDescriptors,testImgDescriptors,matches,mask);
}

void FabMap::compare(const vector<Mat>& queryImgDescriptors, vector<
		IMatch>& matches, bool addQuery, const Mat& mask) {

	// TODO: add first query if empty (is this necessary)

	for (size_t i = 0; i < queryImgDescriptors.size(); i++) {
		CV_Assert(!queryImgDescriptors[i].empty());
		CV_Assert(queryImgDescriptors[i].rows == 1);
		CV_Assert(queryImgDescriptors[i].cols == clTree.cols);
		CV_Assert(queryImgDescriptors[i].type() == CV_32F);

		// TODO: add mask

		compareImgDescriptor(queryImgDescriptors[i],
				i, testImgDescriptors, matches);
		if (addQuery)
				add(queryImgDescriptors[i]);
	}
}

void FabMap::compare(const vector<Mat>& queryImgDescriptors,
		const vector<Mat>& testImgDescriptors,
		vector<IMatch>& matches, const Mat& mask) {

	if (&testImgDescriptors != &(this->testImgDescriptors)) {
		CV_Assert(!(flags & MOTION_MODEL));
		for (size_t i = 0; i < testImgDescriptors.size(); i++) {
			CV_Assert(!testImgDescriptors[i].empty());
			CV_Assert(testImgDescriptors[i].rows == 1);
			CV_Assert(testImgDescriptors[i].cols == clTree.cols);
			CV_Assert(testImgDescriptors[i].type() == CV_32F);
		}
	}

	for (size_t i = 0; i < queryImgDescriptors.size(); i++) {
		CV_Assert(!queryImgDescriptors[i].empty());
		CV_Assert(queryImgDescriptors[i].rows == 1);
		CV_Assert(queryImgDescriptors[i].cols == clTree.cols);
		CV_Assert(queryImgDescriptors[i].type() == CV_32F);

		// TODO: add mask

		compareImgDescriptor(queryImgDescriptors[i],
				i, testImgDescriptors, matches);
	}
}

// IMPORTANT
void FabMap::compareImgDescriptor(const Mat& queryImgDescriptor,
		int queryIndex, const vector<Mat>& testImgDescriptors,
		vector<IMatch>& matches) {

	vector<IMatch> queryMatches;
	queryMatches.push_back(IMatch(queryIndex,-1,
		getNewPlaceLikelihood(queryImgDescriptor),0));

	// getLikelihoods compute log-likehoods of query image (stored in queryImgDescriptor) in terms of known locations ( stored in testImgDescriptors)
	// the result is returned in queryMatches as a N-by-4 matrix
	// each row of queryMatches represents a computation result
	// col0: query index ( Z_k )
	// col1: test image index (location) L_i
	// col2: log likelihood of observation log( P( Z_k|L_i))
	// col3: normalized probability
	getLikelihoods(queryImgDescriptor,testImgDescriptors,queryMatches);
	normaliseDistribution(queryMatches);
	for (size_t j = 1; j < queryMatches.size(); j++) {
		queryMatches[j].queryIdx = queryIndex;
	}
	matches.insert(matches.end(), queryMatches.begin(), queryMatches.end());
}

void FabMap::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

}

double FabMap::getNewPlaceLikelihood(const Mat& queryImgDescriptor) {
		// not sure about the MEAN_FIELD thing
	if (flags & MEAN_FIELD) {
		double logP = 0;
		bool zq, zpq;
		if(flags & NAIVE_BAYES) {
			for (int q = 0; q < clTree.cols; q++) {
				// zq is the parent of q
				// if q is the root of the cltree, its parent is itself
				// determine whether observation zq exists in the query image 
				zq = queryImgDescriptor.at<float>(0,q) > 0;

				// compute probability P(zq)
				// P(eq=false)*P(zq|eq=false) + P(eq=true)*p(zq|eq=true)
		logP += log(Pzq(q, false) * PzqGeq(zq, false) +
						Pzq(q, true) * PzqGeq(zq, true));
			}
			// Since naive bayes method assume each observation is independent on each other
			// so the probability P(Z_k)=p(z_1)*p(z_2)*...*p(z_v), where v is the size of vocabulary
			// logP = log( P(Z_k) )
		} else {
				
			for (int q = 0; q < clTree.cols; q++) {
				zq = queryImgDescriptor.at<float>(0,q) > 0;
				zpq = queryImgDescriptor.at<float>(0,pq(q)) > 0;

				double alpha, beta, p;
				alpha = Pzq(q, zq) * PzqGeq(!zq, false) * PzqGzpq(q, !zq, zpq);
				beta = Pzq(q, !zq) * PzqGeq(zq, false) * PzqGzpq(q, zq, zpq);
				p = Pzq(q, false) * beta / (alpha + beta);
				// P(eq=F)*P(zq|eq=F, zpq)

				alpha = Pzq(q, zq) * PzqGeq(!zq, true) * PzqGzpq(q, !zq, zpq);
				beta = Pzq(q, !zq) * PzqGeq(zq, true) * PzqGzpq(q, zq, zpq);
				p += Pzq(q, true) * beta / (alpha + beta);
				// P(eq=T)*P(zq|eq=T, zpq)

				logP += log(p);
			}
			// logP = log( Product of all P(zq|zpq) including P(zr) )
		}
		return logP;
	}

	if (flags & SAMPLED) {
		CV_Assert(!trainingImgDescriptors.empty());
		CV_Assert(numSamples > 0);

		vector<Mat> sampledImgDescriptors;

		// TODO: this method can result in the same sample being added
		// multiple times. Is this desired?

		for (int i = 0; i < numSamples; i++) {
			int index = rand() % trainingImgDescriptors.size();
			sampledImgDescriptors.push_back(trainingImgDescriptors[index]);
		}

		vector<IMatch> matches;
		getLikelihoods(queryImgDescriptor,sampledImgDescriptors,matches);

		// while averageLogLikelihood is initialized like this
		// seems matches.front().likehood is added twice
		// desirable?
		// seems DBL_MAX is used for numerical reasons
		// ???
		double averageLogLikelihood = -DBL_MAX + matches.front().likelihood + 1;
		for (int i = 0; i < numSamples; i++) {
			averageLogLikelihood = 
				logsumexp(matches[i].likelihood, averageLogLikelihood);
		}

		return averageLogLikelihood - log((double)numSamples);
		// averageLogLikelihood = log( sum( P(Z_k| sampled_locations) ) / numSamples)
		// seems lack the probability of new location given previous observations
		// e.g. P(L_new|Z^{k-1})
		// aha, maybe the author assume P(L_new|Z^{k-1})=1, so the first term of equation 3.21 in MJCThesis is ignored
		// in MJCTheis, it is said " In practive we have found it more convenient to use random collections of images for the sampling set, so have not taken advantage of this possibility"
		// Therefore the actual formulation for P(Z_k|Z^{k-1}) is
		// P(Z_k|Z^{k-1})=sum( P(Z_k| sampled_locations) / numSamples
	}
	return 0;
}

void FabMap::normaliseDistribution(vector<IMatch>& matches) {
	CV_Assert(!matches.empty());

	if (flags & MOTION_MODEL) {

		matches[0].match = matches[0].likelihood + log(Pnew);

		if (priorMatches.size() > 2) {
			matches[1].match = matches[1].likelihood;
			matches[1].match += log(
				(2 * (1-mBias) * priorMatches[1].match +
				priorMatches[1].match +
				2 * mBias * priorMatches[2].match) / 3);
			for (size_t i = 2; i < priorMatches.size()-1; i++) {
				matches[i].match = matches[i].likelihood;
				matches[i].match += log(
					(2 * (1-mBias) * priorMatches[i-1].match +
					priorMatches[i].match +
					2 * mBias * priorMatches[i+1].match)/3);
			}
			matches[priorMatches.size()-1].match = 
				matches[priorMatches.size()-1].likelihood;
			matches[priorMatches.size()-1].match += log(
				(2 * (1-mBias) * priorMatches[priorMatches.size()-2].match +
				priorMatches[priorMatches.size()-1].match + 
				2 * mBias * priorMatches[priorMatches.size()-1].match)/3);

			for(size_t i = priorMatches.size(); i < matches.size(); i++) {
				matches[i].match = matches[i].likelihood;
			}
		} else {
			for(size_t i = 1; i < matches.size(); i++) {
				matches[i].match = matches[i].likelihood;
			}
		}

		double logsum = -DBL_MAX + matches.front().match + 1;

		//calculate the normalising constant
		for (size_t i = 0; i < matches.size(); i++) {
			logsum = logsumexp(logsum, matches[i].match);
		}

		//normalise
		for (size_t i = 0; i < matches.size(); i++) {
			matches[i].match = exp(matches[i].match - logsum);
		}

		//smooth final probabilities
		for (size_t i = 0; i < matches.size(); i++) {
			matches[i].match = sFactor*matches[i].match +
			(1 - sFactor)/matches.size();
		}

		//update our location priors
		priorMatches = matches;

	} else {

			// without motion modeli, term P(L_i|Z^{k-1}) is ignored
			// thus the location likelihood is:
			// P(L_i|Z^k)=P(Z_k|L_i,Z^{k-1})/P(Z_k|Z^{k-1})
			// Also, since no update is done in data association, the condition Z^{k-1} can be ignored,
			// just use probability stored in row 1 of clTree.
			// And the new place likelihood is computed by randomly sampling.
			// Finally, the location likelihood is:
			// P(L_i|Z^k)=P(L_i|clTree)=P(Z_k|L_i, clTree)/P(Z_k|randomly_sampled_image_descriptors)
			// since -DBL_MAX is so "large" that adding a term like matches.front().likelihood basically doesn't affect its vale, thus exp( logsum )=exp( -DBL_MAX )=0
			// the assignment below is just used for initializing log of null possibility, which is log( zero ) !!


		double logsum = -DBL_MAX + matches.front().likelihood + 1;

		for (size_t i = 0; i < matches.size(); i++) {
			logsum = logsumexp(logsum, matches[i].likelihood);
		}
		for (size_t i = 0; i < matches.size(); i++) {
			matches[i].match = exp(matches[i].likelihood - logsum);
		}
		// this normalization process is just different from the MJCThesis
		// in his paper P(Li|Z^k)= (P(Z_k|Li)*P(Li|Z^{k-1}))/P(Z_k|Z^{k-1})
		// here we compute P0=P(new place) and Pi=P(Li|Z^k), where i runs from 1 to N
		// thus probability of each event is
		// Pi = Pi/ sum( Pi ), i runs from 0 to N
		for (size_t i = 0; i < matches.size(); i++) {
			matches[i].match = sFactor*matches[i].match +
			(1 - sFactor)/matches.size();
		}
	}
}

int FabMap::pq(int q) {
	return (int)clTree.at<double>(0,q);
}

double FabMap::Pzq(int q, bool zq) {
		// compute P(zq) = P(eq)
		// because of the data association strategy we adopted
		// P(zq) = P(eq)
	return (zq) ? clTree.at<double>(1,q) : 1 - clTree.at<double>(1,q);
}

double FabMap::PzqGzpq(int q, bool zq, bool zpq) {
		// compute P(zq|zpq)
	if (zpq) {
		return (zq) ? clTree.at<double>(2,q) : 1 - clTree.at<double>(2,q);
	} else {
		return (zq) ? clTree.at<double>(3,q) : 1 - clTree.at<double>(3,q);
	}
}

double FabMap::PzqGeq(bool zq, bool eq) {
		// detector model
		// P(zq|eq)
	if (eq) {
		return (zq) ? PzGe : 1 - PzGe;
	} else {
		return (zq) ? PzGNe : 1 - PzGNe;
	}
}

double FabMap::PeqGL(int q, bool Lzq, bool eq) {
		// compute P(eq|Li)
		// p(eq=s|Li) = P(eq=s, Li) / P(Li)
		// 			= P(Li|eq=s)*P(eq=s) / [ P(Li|eq=s)*p(eq=s) + P(Li|eq!=s)*P(eq!=s) ]
		// assume zq is independent on ep where p != q, so
		// we can substitute Li with zq at location Li
		// L_zq is representation of zq at Li
	double alpha, beta;
	// ??? weird
	// what's stored in row 1 of clTree should be the probability of the observation z rather event e
	// BUT the following codes show that it's e instead of z
	// ??? p(eq) = P(zq) ???
	// alpha = P(L_zq|eq=T)*P(zq=T) approximate using P(L_zq|eq=T)*P(eq=T) ???
	// beta  = P(L_zq|eq=F)*P(zq=F)
	// ACTUALLY in MJCThesis, it is said that the data association strategy is:
	// if it's an unknown location, create a new location
	// if it's a previously visited location, DON'T UPDATE
	// therefore what's stored in row 1 of clTree is actually P(eq)=P(zq) and will not change in the data association process
	alpha = PzqGeq(Lzq, true) * Pzq(q, true);
	beta = PzqGeq(Lzq, false) * Pzq(q, false);

	if (eq) {
		return alpha / (alpha + beta);
	} else {
		return 1 - (alpha / (alpha + beta));
	}
}

double FabMap::PzqGL(int q, bool zq, bool zpq, bool Lzq) {
		// compute p(zq|Li) under the naive bayes assumption
		// p(zq|Li) = P(eq=F|Li)*P(zq|eq=F)+P(eq=T|Li)*P(zq|eq=T)
	return PeqGL(q, Lzq, false) * PzqGeq(zq, false) + 
		PeqGL(q, Lzq, true) * PzqGeq(zq, true);
}


double FabMap::PzqGzpqL(int q, bool zq, bool zpq, bool Lzq) {
		// compute P(zq|zpq, Li)
	double p;
	double alpha, beta;

	// P(eq=F|Li)*P(zq|eq=F, zpq)
	alpha = Pzq(q,  zq) * PzqGeq(!zq, false) * PzqGzpq(q, !zq, zpq);
	beta  = Pzq(q, !zq) * PzqGeq( zq, false) * PzqGzpq(q,  zq, zpq);
	p = PeqGL(q, Lzq, false) * beta / (alpha + beta);

	// P(eq=T|Li)*P(zq|eq=T, zpq)
	alpha = Pzq(q,  zq) * PzqGeq(!zq, true) * PzqGzpq(q, !zq, zpq);
	beta  = Pzq(q, !zq) * PzqGeq( zq, true) * PzqGzpq(q,  zq, zpq);
	p += PeqGL(q, Lzq, true) * beta / (alpha + beta);

	return p;
}


FabMap1::FabMap1(const Mat& _clTree, double _PzGe, double _PzGNe, int _flags,
		int _numSamples) : FabMap(_clTree, _PzGe, _PzGNe, _flags,
				_numSamples) {
}

FabMap1::~FabMap1() {
}

void FabMap1::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

	for (size_t i = 0; i < testImgDescriptors.size(); i++) {
		bool zq, zpq, Lzq;
		double logP = 0;
		// logP = log( P(Z_k|L_i) )
		// log-likelihood of the query image given location i
		for (int q = 0; q < clTree.cols; q++) {
			
			zq = queryImgDescriptor.at<float>(0,q) > 0;
			zpq = queryImgDescriptor.at<float>(0,pq(q)) > 0;
			// Lzq=T then feature eq is observed at location Li
			// otherwise feature eq is not observed at location Li
			Lzq = testImgDescriptors[i].at<float>(0,q) > 0;

			// PzGL is pointed to different functions according to
			// naive bayes OR cltree
			// ref line 58
			logP += log((this->*PzGL)(q, zq, zpq, Lzq));

		}
		matches.push_back(IMatch(0,i,logP,0));
	}
}

FabMapLUT::FabMapLUT(const Mat& _clTree, double _PzGe, double _PzGNe,
		int _flags, int _numSamples, int _precision) :
FabMap(_clTree, _PzGe, _PzGNe, _flags, _numSamples), precision(_precision) {

	int nWords = clTree.cols;
	double precFactor = (double)pow(10.0, precision);

	table = new int[nWords][8];

	for (int q = 0; q < nWords; q++) {
		for (unsigned char i = 0; i < 8; i++) {

			bool Lzq = (bool) ((i >> 2) & 0x01);
			bool zq = (bool) ((i >> 1) & 0x01);
			bool zpq = (bool) (i & 1);

			table[q][i] = -(int)(log((this->*PzGL)(q, zq, zpq, Lzq))
					* precFactor);
		}
	}
}

FabMapLUT::~FabMapLUT() {
	delete[] table;
}

void FabMapLUT::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

	double precFactor = (double)pow(10.0, -precision);

	for (size_t i = 0; i < testImgDescriptors.size(); i++) {
		unsigned long long int logP = 0;
		for (int q = 0; q < clTree.cols; q++) {
			logP += table[q][(queryImgDescriptor.at<float>(0,pq(q)) > 0) +
			((queryImgDescriptor.at<float>(0, q) > 0) << 1) +
			((testImgDescriptors[i].at<float>(0,q) > 0) << 2)];
		}
		matches.push_back(IMatch(0,i,-precFactor*(double)logP,0));
	}
}

FabMapFBO::FabMapFBO(const Mat& _clTree, double _PzGe, double _PzGNe,
		int _flags, int _numSamples, double _rejectionThreshold,
		double _PsGd, int _bisectionStart, int _bisectionIts) :
FabMap(_clTree, _PzGe, _PzGNe, _flags, _numSamples), PsGd(_PsGd),
	rejectionThreshold(_rejectionThreshold), bisectionStart(_bisectionStart),
		bisectionIts(_bisectionIts) {
}


FabMapFBO::~FabMapFBO() {
}

void FabMapFBO::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

	multiset<WordStats> wordData;
	setWordStatistics(queryImgDescriptor, wordData);

	vector<int> matchIndices;
	vector<IMatch> queryMatches;

	for (size_t i = 0; i < testImgDescriptors.size(); i++) {
		queryMatches.push_back(IMatch(0,i,0,0));
		matchIndices.push_back(i);
	}

	double currBest;
	double bailedOut = DBL_MAX;

	for (multiset<WordStats>::iterator wordIter = wordData.begin();
			wordIter != wordData.end(); wordIter++) {
		bool zq = queryImgDescriptor.at<float>(0,wordIter->q) > 0;
		bool zpq = queryImgDescriptor.at<float>(0,pq(wordIter->q)) > 0;

		currBest = -DBL_MAX;

		// for a fixed word compute likelihood in parallel
		// call this PARALLEL loop
		for (size_t i = 0; i < matchIndices.size(); i++) {
			bool Lzq = 
				testImgDescriptors[matchIndices[i]].at<float>(0,wordIter->q) > 0;
			queryMatches[matchIndices[i]].likelihood +=
				log((this->*PzGL)(wordIter->q,zq,zpq,Lzq));
			// find current maximum likelihood
			currBest = 
				std::max(queryMatches[matchIndices[i]].likelihood, currBest);
		}

		if (matchIndices.size() == 1)
			continue;

		// solve inequality 4.9
		double delta = std::max(limitbisection(wordIter->V, wordIter->M), 
			-log(rejectionThreshold));

		vector<int>::iterator matchIter = matchIndices.begin();
		while (matchIter != matchIndices.end()) {
			if (currBest - queryMatches[*matchIter].likelihood > delta) {
					// if the query match (test hypothesis) pointed by matchIter has very little possibility
					// to take over the current best hypothesis
					// Then bail-out this hypothesis
				queryMatches[*matchIter].likelihood = bailedOut;
					// And erase the hypothesis, which means will not compute it in the next PARALLEL loop
				matchIter = matchIndices.erase(matchIter);
			} else {
					// if the query hypothesis is possible to take over the current best
					// keep it
				matchIter++;
			}
		}
	}

	for (size_t i = 0; i < queryMatches.size(); i++) {
		if (queryMatches[i].likelihood == bailedOut) {
			queryMatches[i].likelihood = currBest + log(rejectionThreshold);
		}
	}
	matches.insert(matches.end(), queryMatches.begin(), queryMatches.end());

}

void FabMapFBO::setWordStatistics(const Mat& queryImgDescriptor,
	multiset<WordStats>& wordData) {
	//words are sorted according to information = -ln(P(zq|zpq))
	//in non-log format this is lowest probability first
	for (int q = 0; q < clTree.cols; q++) {
		wordData.insert(WordStats(q,PzqGzpq(q,
				queryImgDescriptor.at<float>(0,q) > 0,
				queryImgDescriptor.at<float>(0,pq(q)) > 0)));
	}

	double d = 0, V = 0, M = 0;
	bool zq, zpq;

	// iteration in reverse order to compute
	// (1) the sum of variation afterwards AND
	// (2) maximum absolute of Xi afterwards
	for (multiset<WordStats>::reverse_iterator wordIter = wordData.rbegin();
			wordIter != wordData.rend(); wordIter++) {

		zq = queryImgDescriptor.at<float>(0,wordIter->q) > 0;
		zpq = queryImgDescriptor.at<float>(0,pq(wordIter->q)) > 0;

		// d = log( P(zq|zpq, zq in Li) ) - log( P(zq|zpq, zq not in Li) )
		d = log((this->*PzGL)(wordIter->q, zq, zpq, true)) - 
			log((this->*PzGL)(wordIter->q, zq, zpq, false));

		// v = sum( E[Xi^2] )
		// according to equation 4.12, Xi has the distribution of:
		// p(Xi=d)=u(1-u)
		// P(Xi=0)=(1-u)^2+u^2
		// P(Xi=-d)=u(1-u)
		// Therefore E[Xi^2]=d^2*2*u(1-u)
		// Where u is the probability of feature zq observed at location 
		V += pow(d, 2.0) * 2 * 
			(Pzq(wordIter->q, true) - pow(Pzq(wordIter->q, true), 2.0));
		// M is just the maximum absolute value of Xi
		M = std::max(M, fabs(d));

		wordIter->V = V;
		wordIter->M = M;
	}
}

// find solution of the inequality 4.9
// Since P( S > delta ) < bennettInequality(v, m, delta),
// when we solve the equation bennettInequality(v, m, delta) = PsGd ( PsGd is the user specified error? )
// then we are sure to claim that P( S > delta ) < PsGd
// thus the testing hypothesis has little possibility to take over the leading hypothesis ( which advances by delta )
// Because its possibility to get more likelihood ( S ) than delta is less than PsGd, which is a user specificed small number
double FabMapFBO::limitbisection(double v, double m) {
	double midpoint, left_val, mid_val;
	double left = 0, right = bisectionStart;

	left_val = bennettInequality(v, m, left) - PsGd;

	for(int i = 0; i < bisectionIts; i++) {

		midpoint = (left + right)*0.5;
		mid_val = bennettInequality(v, m, midpoint)- PsGd;

		if(left_val * mid_val > 0) {
			left = midpoint;
			left_val = mid_val;
		} else {
			right = midpoint;
		}
	}

	return (right + left) * 0.5;
}

// computate formulation
// exp( v / (m^2) * cosh( f( Delta ) ) - 1 - Delta * m / v * f( Delta )
double FabMapFBO::bennettInequality(double v, double m, double delta) {
	double DMonV = delta * m / v; 
	// f( Delta ) = sinh^{-1}( DDelta * m / v);
	// sinh( x ) = (e^x - e^{-x})/2
	// sinh^{-1}( x ) = log( x + sqrt(x^2 + 1) )
	double f_delta = log(DMonV + sqrt(pow(DMonV, 2.0) + 1));
	return exp((v / pow(m, 2.0))*(cosh(f_delta) - 1 - DMonV * f_delta));
}

bool FabMapFBO::compInfo(const WordStats& first, const WordStats& second) {
	return first.info < second.info;
}

FabMap2::FabMap2(const Mat& _clTree, double _PzGe, double _PzGNe,
		int _flags) :
FabMap(_clTree, _PzGe, _PzGNe, _flags) {
	CV_Assert(flags & SAMPLED);

	children.resize(clTree.cols);

	for (int q = 0; q < clTree.cols; q++) {
		// PzGL(q, zq, zpq, Li) =  P(zq|zpq, whether zq exists in Li)

		// d1, d2, d3, d4 represent normalized likelihoods of the four conditions ( zq=F/T | zpq=F/T )
		// the normalization term is the probability that above event happens conditioned on negative observation of zq at location i
		// e.g. P( zq|zpq, Li=F)
		// This term is used to be denominator because it's the most often condition that a certain feature is not observed at a given location
		// and once normalized like this, sparse pattern could be found (i.e. lots of zeros will appear)

	    // d1: log( P(zq=F|zpq=F, Lzq=T) / P(zq=F|zpq=F, Lzq=F) )
		d1.push_back(log((this->*PzGL)(q, false, false, true) /
				(this->*PzGL)(q, false, false, false)));

		// The reason to substract d1 from d2, d3 and d4 is that
		// in updating log-likelihood, we can simply add d2 ( or d3, d4) to the default log-likelihood
		// and no need to substract d1 in the following computation ( which will be very often)
		// this strategy is just used for reducing computing time

	    // d2: log( P(zq=F|zpq=T, Lzq=T) / P(zq=F|zpq=T, Lzq=F) ) - d1
		d2.push_back(log((this->*PzGL)(q, false, true, true) /
				(this->*PzGL)(q, false, true, false)) - d1[q]);
		// d3: log( P(zq=T|zpq=F, Lzq=T) / P(zq=T|zpq=F, Lzq=F) ) - d1
		d3.push_back(log((this->*PzGL)(q, true, false, true) /
				(this->*PzGL)(q, true, false, false))- d1[q]);
	    // d4: log( P(zq=T|zpq=T, Lzq=T) / P(zq=T|zpq=T, Lzq=F) ) - d1
		d4.push_back(log((this->*PzGL)(q, true, true, true) /
				(this->*PzGL)(q, true, true, false))- d1[q]);
		// children[i] records children of node i
		children[pq(q)].push_back(q);
	}

}

FabMap2::~FabMap2() {
}


void FabMap2::addTraining(const vector<Mat>& queryImgDescriptors) {
	for (size_t i = 0; i < queryImgDescriptors.size(); i++) {
		CV_Assert(!queryImgDescriptors[i].empty());
		CV_Assert(queryImgDescriptors[i].rows == 1);
		CV_Assert(queryImgDescriptors[i].cols == clTree.cols);
		CV_Assert(queryImgDescriptors[i].type() == CV_32F);
		// add image descriptors to training set ( used for randomly sampling to compute new place likelihood )
		trainingImgDescriptors.push_back(queryImgDescriptors[i]);
		addToIndex(queryImgDescriptors[i], trainingDefaults, trainingInvertedMap);
	}
}


void FabMap2::add(const vector<Mat>& queryImgDescriptors) {
	// add test image descriptors to testImgDescriptors
	// and compute default log-likelihoods of each location
	for (size_t i = 0; i < queryImgDescriptors.size(); i++) {
		CV_Assert(!queryImgDescriptors[i].empty());
		CV_Assert(queryImgDescriptors[i].rows == 1);
		CV_Assert(queryImgDescriptors[i].cols == clTree.cols);
		CV_Assert(queryImgDescriptors[i].type() == CV_32F);
		testImgDescriptors.push_back(queryImgDescriptors[i]);
		// add image descriptors to test set ( test set is a history of previously visited locations)
		// testDefaults stores the default likelihood of a location which is sum ( log (P(zq=F|zpq=F, zq=T) / P(zq=F|zpq=F, zq=F) ) )
		// testInvertedMap stores the inverted map of each feature i.e. feature -> locations where feature is observed
		addToIndex(queryImgDescriptors[i], testDefaults, testInvertedMap);
	}
}

void FabMap2::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

	if (&testImgDescriptors== &(this->testImgDescriptors)) {
		getIndexLikelihoods(queryImgDescriptor, testDefaults, testInvertedMap, 
			matches);
	} else {
		CV_Assert(!(flags & MOTION_MODEL));
		vector<double> defaults;
		map<int, vector<int> > invertedMap;
		for (size_t i = 0; i < testImgDescriptors.size(); i++) {
			// compute default likelihood of the query image
			addToIndex(testImgDescriptors[i],defaults,invertedMap);
		}
		getIndexLikelihoods(queryImgDescriptor, defaults, invertedMap, matches);
	}
}

double FabMap2::getNewPlaceLikelihood(const Mat& queryImgDescriptor) {

	CV_Assert(!trainingImgDescriptors.empty());

	vector<IMatch> matches;
	getIndexLikelihoods(queryImgDescriptor, trainingDefaults,
			trainingInvertedMap, matches);

	double averageLogLikelihood = -DBL_MAX + matches.front().likelihood + 1;
	for (size_t i = 0; i < matches.size(); i++) {
		averageLogLikelihood = 
			logsumexp(matches[i].likelihood, averageLogLikelihood);
	}

	return averageLogLikelihood - log((double)trainingDefaults.size());

}

void FabMap2::addToIndex(const Mat& queryImgDescriptor,
		vector<double>& defaults,
		map<int, vector<int> >& invertedMap) {
	defaults.push_back(0);
	for (int q = 0; q < clTree.cols; q++) {
		// if zq exists at location L, add d1
		// to default location log-likelihood 
		if (queryImgDescriptor.at<float>(0,q) > 0) {
			// if visual word zq is observed in the query image
			// add log( P(zq=F|zpq=F, Lzq=T) / P(zq=F|zpq=F, Lzq=F) ) to the default
			// likelihood of the new location
			// then update? seems still need to substract this term ... so sad
			defaults.back() += d1[q];
			invertedMap[q].push_back((int)defaults.size()-1);
		}
	}
}

void FabMap2::getIndexLikelihoods(const Mat& queryImgDescriptor,
		vector<double>& defaults,
		map<int, vector<int> >& invertedMap,
		vector<IMatch>& matches) {

	vector<int>::iterator LwithI, child;

	std::vector<double> likelihoods = defaults;

	    // d1: log( P(zq=F|zpq=F, Lzq=T) / P(zq=F|zpq=F, Lzq=F) )
	    // d2: log( P(zq=F|zpq=T, Lzq=T) / P(zq=F|zpq=T, Lzq=F) ) - d1
		// d3: log( P(zq=T|zpq=F, Lzq=T) / P(zq=T|zpq=F, Lzq=F) ) - d1
	    // d4: log( P(zq=T|zpq=T, Lzq=T) / P(zq=T|zpq=T, Lzq=F) ) - d1
	for (int q = 0; q < clTree.cols; q++) {
		if (queryImgDescriptor.at<float>(0,q) > 0) {
			for (LwithI = invertedMap[q].begin(); 
				LwithI != invertedMap[q].end(); LwithI++) {

				// update log( P(Li|Z^k) )
				if (queryImgDescriptor.at<float>(0,pq(q)) > 0) {
					// += log( P(zq=T|zpq=T, Lzq=T) / P(zq=T|zpq=T, Lzq=F) ) - log( P(zq=F|zpq=F, Lzq=T) / p(zq=F|zpq=F, Lzq=F) )
					likelihoods[*LwithI] += d4[q];
				} else {
					// += log( P(zq=T|zpq=F, Lzq=T) / P(zq=T|zpq=F, Lzq=F) ) - log( P(zq=F|zpq=F, Lzq=T) / p(zq=F|zpq=F, Lzq=F) )
					likelihoods[*LwithI] += d3[q];
				}
			}
			for (child = children[q].begin(); child != children[q].end();
				child++) {

				if (queryImgDescriptor.at<float>(0,*child) == 0) {
					for (LwithI = invertedMap[*child].begin();
						LwithI != invertedMap[*child].end(); LwithI++) {

						likelihoods[*LwithI] += d2[*child];
					}
				}
			}
		}
	}

	for (size_t i = 0; i < likelihoods.size(); i++) {
		matches.push_back(IMatch(0,i,likelihoods[i],0));
	}
}

}
/*
   compute the likelihoods of all hypotheses in parallel and terminate the likelihood calculation for hypotheses that have fallen too far behind the current leading hypothesis
   */
