/** @file cafe_shell.c
* @brief  Functions corresponding to the commands available in Cafe.
*
* Command list is found in the #cafe_cmd function
*/

#include<mathfunc.h>
#include<ctype.h>
#include <assert.h>
#include <float.h>
#include<stdarg.h>
#include<stdio.h>
#include "cafe.h"
#include "cafe_shell.h"
#include "viterbi.h"

extern int cafe_shell_dispatch_commandf(char* format, ...);
extern pBirthDeathCacheArray probability_cache;

/**
* \brief Holds the global program state that user commands act on.
*
*/
pCafeParam cafe_param;

pTree tmp_lambda_tree;

#ifndef STDERR_IF
	#define STDERR_IF(a,b)	if ( a ) { fprintf(stderr,b); return -1; }
#endif

/**
* \brief Holds the list of commands that are available in Cafe.
* 
* Each element consists of a command and the function that is 
* called to handle that command. Terminated by a NULL, NULL pair
* Functions include #cafe_cmd_lambda, #cafe_cmd_family, #cafe_cmd_tree,
* etc.
*/
CafeShellCommand cafe_cmd[]  =
{
	{ "lambdamu", cafe_cmd_lambda_mu },
	{ "rootdist", cafe_cmd_root_dist}, 
	{ "cvspecies", cafe_cmd_crossvalidation_by_species},
	{ "cvfamily", cafe_cmd_crossvalidation_by_family},
	{ "simextinct", cafe_cmd_sim_extinct },
	{ NULL, NULL }
};

pArrayList cafe_shell_build_argument(int argc, char* argv[])
{
	int i, j;
	pArrayList pal = arraylist_new(20);
	for ( i = 1 ; i < argc ; i++ )
	{
		if ( argv[i][0] == '-' && !isdigit(argv[i][1]) )
		{
			pArgument parg = (pArgument)memory_new(1,sizeof(Argument));
			parg->argc = 0;
			parg->opt = argv[i];
			for ( j = i+1; j < argc; j++ )
			{
				if ( argv[j][0] == '-' && !isdigit(argv[j][1]) ) break;
				parg->argc++;
			}
			parg->argv = parg->argc ? &argv[i+1]  : NULL;
			arraylist_add( pal, parg );
			i = j - 1;
		}
	}
	return pal;
}


pArgument cafe_shell_get_argument(char* opt, pArrayList pal)
{
	int i;
	for ( i = 0 ; i < pal->size; i++ )
	{
		pArgument parg = (pArgument)pal->array[i];
		if ( !strcasecmp(parg->opt, opt) )
		{
			return parg;
		}
	}
	return NULL;
}


void cafe_shell_set_lambda(pCafeParam param, double* lambda);
void cafe_shell_set_lambda_mu(pCafeParam param, double* parameters);

void viterbi_parameters_init(viterbi_parameters *viterbi, int nnodes, int nrows)
{
	viterbi->num_nodes = nnodes;
	viterbi->num_rows = nrows;
	viterbi->viterbiPvalues = (double**)memory_new_2dim(nnodes, nrows, sizeof(double));
	viterbi->expandRemainDecrease = (int**)memory_new_2dim(3, nnodes, sizeof(int));
	viterbi->viterbiNodeFamilysizes = (int**)memory_new_2dim(nnodes, nrows, sizeof(int));
	viterbi->maximumPvalues = (double*)memory_new(nrows, sizeof(double));
	viterbi->averageExpansion = (double*)memory_new(nnodes, sizeof(double));
}

void viterbi_parameters_clear(viterbi_parameters* viterbi, int nnodes)
{
//	viterbi_parameters* viterbi = &param->viterbi;
	if ( viterbi->viterbiPvalues )
	{
		int num = (nnodes - 1 )/2;
		memory_free_2dim((void**)viterbi->viterbiPvalues,num,0,NULL);
		memory_free_2dim((void**)viterbi->expandRemainDecrease,3,0,NULL);
		memory_free_2dim((void**)viterbi->viterbiNodeFamilysizes,num, 0, NULL);
		memory_free(viterbi->averageExpansion);
		viterbi->averageExpansion = NULL;
		if ( viterbi->maximumPvalues )
		{
			memory_free(viterbi->maximumPvalues);
			viterbi->maximumPvalues = NULL;
		}
	}
	if ( viterbi->cutPvalues )
	{
		memory_free_2dim((void**)viterbi->cutPvalues,nnodes,0,NULL);
	}
	viterbi->viterbiPvalues = NULL;
	viterbi->expandRemainDecrease = NULL;
	viterbi->viterbiNodeFamilysizes = NULL;
	viterbi->cutPvalues = NULL;
	viterbi->maximumPvalues = NULL;
}

void viterbi_set_max_pvalue(viterbi_parameters* viterbi, int index, double val)
{
	assert(index < viterbi->num_rows);
	viterbi->maximumPvalues[index] = val;
}

void cafe_shell_clear_param(pCafeParam param, int btree_skip)
{
	if ( param->str_fdata ) 
	{
		string_free( param->str_fdata );
		param->str_fdata = NULL;
	}
	if ( param->ML )
	{
		memory_free(param->ML);
		param->ML = NULL;
	}
	if ( param->MAP )
	{
		memory_free(param->MAP);
		param->MAP = NULL;
	}
	if ( param->prior_rfsize )
	{
		memory_free(param->prior_rfsize);
		param->prior_rfsize = NULL;
	}
	
	int nnodes = param->pcafe ? ((pTree)param->pcafe)->nlist->size : 0;
	viterbi_parameters_clear(&param->viterbi, nnodes);
	if ( !btree_skip && param->pcafe ) 
	{
		if (probability_cache)
		{
			birthdeath_cache_array_free(probability_cache);
		}
		cafe_tree_free(param->pcafe);
		//memory_free( param->branchlengths_sorted );
		//param->branchlengths_sorted = NULL;
		memory_free( param->old_branchlength );
		param->old_branchlength = NULL;
		param->pcafe = NULL;
	}
	if ( param->pfamily ) 
	{
		cafe_family_free(param->pfamily);
		param->pfamily = NULL;
	}
	if ( param->parameters ) 
	{
		memory_free( param->parameters );	
		param->parameters = NULL;
	}
	if ( param->lambda ) 
	{
		//memory_free( param->lambda ); param->lambda points to param->parameters
		param->lambda = NULL;
	}
	if ( param->mu ) 
	{
		//memory_free( param->mu );	param->mu points to param->parameters
		param->mu = NULL;
	}
	if ( param->lambda_tree ) 
	{
		phylogeny_free(param->lambda_tree);
		param->lambda_tree = NULL;
	}
	if ( param->mu_tree ) 
	{
		phylogeny_free(param->mu_tree);
		param->mu_tree = NULL;
	}
	
	param->eqbg = 0;
	param->posterior = 0;
    param->num_params = 0;
	param->num_lambdas = 0;
	param->num_mus = 0;
	param->parameterized_k_value = 0;
	param->fixcluster0 = 0;
	param->family_size.root_min = 0;
	param->family_size.root_max = 1;
	param->family_size.min = 0;
	param->family_size.max = 1;
	param->param_set_func = cafe_shell_set_lambda;
	param->num_threads = 1;
	param->num_random_samples = 1000;
	param->pvalue = 0.01;
}

#if 0
void cafe_shell_set_sizes(pCafeParam param)
{
	pCafeTree pcafe = param->pcafe;
	copy_range_to_tree(pcafe, &param->family_size);
	cafe_tree_set_parameters(pcafe, &param->family_size, 0 );
}
#endif

void cafe_shell_prompt(char* prompt, char* format, ... )
{
	va_list ap;
	va_start(ap, format);
	printf("%s ", prompt);
	if (vscanf( format, ap ) == EOF)
		fprintf(stderr, "Read failure\n");
	va_end(ap);
}

void reset_k_likelihoods(pCafeNode pcnode, int k, int num_factors)
{
	if (pcnode->k_likelihoods) { memory_free(pcnode->k_likelihoods); pcnode->k_likelihoods = NULL; }
	pcnode->k_likelihoods = (double**)memory_new_2dim(k, num_factors, sizeof(double));
}

void set_birth_death_probabilities(struct probabilities *probs, int num_lambdas, int num_lambdas2, int fix_cluster, double* parameters)
{
	if (num_lambdas < 1)
	{
		probs->lambda = parameters[0];
		probs->mu = parameters[num_lambdas2];
	}
	else
	{
		probs->lambda = -1;
		probs->mu = -1;
		free_probabilities(probs);
		probs->param_lambdas = (double*)memory_new(num_lambdas, sizeof(double));
		if (!fix_cluster) {
			memcpy(&probs->param_lambdas[0], &parameters[0], (num_lambdas)*sizeof(double));
		}
		else {
			probs->param_lambdas[0] = 0;
			memcpy(&probs->param_lambdas[1], &parameters[0], (num_lambdas - fix_cluster)*sizeof(double));
		}

		probs->param_mus = (double*)memory_new(num_lambdas, sizeof(double));
		if (!fix_cluster) {
			memcpy(&probs->param_mus[0], &parameters[num_lambdas2*num_lambdas], (num_lambdas)*sizeof(double));
		}
		else {
			probs->param_mus[0] = 0;
			memcpy(&probs->param_mus[1], &parameters[num_lambdas2*(num_lambdas - fix_cluster)], (num_lambdas - fix_cluster)*sizeof(double));
		}
	}
}

void set_birth_death_probabilities2(struct probabilities *probs, int num_lambdas, int num_lambdas2, int fix_cluster, int taxa_id, int eqbg, double* parameters)
{
	if (num_lambdas > 0) {
		probs->lambda = -1;
		probs->mu = -1;

		free_probabilities(probs);
		// set lambdas
		probs->param_lambdas = (double*)memory_new(num_lambdas, sizeof(double));
		if (!fix_cluster) {
			memcpy(&probs->param_lambdas[0], &parameters[taxa_id*num_lambdas], (num_lambdas)*sizeof(double));
		}
		else {
			probs->param_lambdas[0] = 0;
			memcpy(&probs->param_lambdas[1], &parameters[taxa_id*(num_lambdas - 1)], (num_lambdas - 1)*sizeof(double));
		}

		// set mus
		probs->param_mus = (double*)memory_new(num_lambdas, sizeof(double));
		if (eqbg) {
			if (taxa_id == 0) {
				memcpy(probs->param_mus, probs->param_lambdas, (num_lambdas - fix_cluster)*sizeof(double));
			}
			else {
				if (!fix_cluster) {
					memcpy(&probs->param_mus[0], &parameters[(num_lambdas2)*num_lambdas + (taxa_id - eqbg)*num_lambdas], (num_lambdas)*sizeof(double));
				}
				else {
					probs->param_mus[0] = 0;
					memcpy(&probs->param_mus[1], &parameters[(num_lambdas2)*(num_lambdas - 1) + (taxa_id - eqbg)*(num_lambdas - 1)], (num_lambdas - 1)*sizeof(double));
				}
			}
		}
		else {
			if (!fix_cluster) {
				memcpy(&probs->param_mus[0], &parameters[(num_lambdas2)*num_lambdas + taxa_id*num_lambdas], (num_lambdas)*sizeof(double));
			}
			else {
				probs->param_mus[0] = 0;
				memcpy(&probs->param_mus[1], &parameters[(num_lambdas2)*(num_lambdas - 1) + taxa_id*(num_lambdas - 1)], (num_lambdas - 1)*sizeof(double));
			}
		}
	}
	else
	{
		if (eqbg) {
			probs->lambda = parameters[taxa_id];
			if (taxa_id == 0) {
				probs->mu = probs->lambda;
			}
			else {
				probs->mu = parameters[(num_lambdas2)+(taxa_id - eqbg)];
			}
		}
		else {
			probs->lambda = parameters[taxa_id];
			probs->mu = parameters[(num_lambdas2)+taxa_id];
		}

	}
}


void set_birth_death_probabilities3(struct probabilities *probs, int num_lambdas, int num_lambdas2, int fix_cluster, double* parameters)
{
	if (num_lambdas > 0) {
		probs->lambda = -1;
		probs->mu = -1;
		free_probabilities(probs);

		probs->param_lambdas = (double*)memory_new(num_lambdas, sizeof(double));
		if (!fix_cluster) {
			memcpy(&probs->param_lambdas[0], &parameters[0], (num_lambdas)*sizeof(double));
		}
		else {
			probs->param_lambdas[0] = 0;
			memcpy(&probs->param_lambdas[1], &parameters[0], (num_lambdas - 1)*sizeof(double));
		}

	}
	else {
		probs->lambda = parameters[0];
		probs->mu = -1;
	}

}

void set_birth_death_probabilities4(struct probabilities *probs, int num_lambdas, int num_lambdas2, int fix_cluster, int taxa_id, int eqbg, double* parameters)
{
	if (num_lambdas > 0) {
		probs->lambda = -1;
		probs->mu = -1;
		free_probabilities(probs);

		probs->param_lambdas = (double*)memory_new(num_lambdas, sizeof(double));
		if (!fix_cluster) {
			memcpy(&probs->param_lambdas[0], &parameters[taxa_id*num_lambdas], (num_lambdas)*sizeof(double));
		}
		else {
			probs->param_lambdas[0] = 0;
			memcpy(&probs->param_lambdas[1], &parameters[taxa_id*(num_lambdas - 1)], (num_lambdas - 1)*sizeof(double));
		}

	}
	else {
		probs->lambda = parameters[taxa_id];
		probs->mu = -1;
	}
}

void cafe_shell_set_lambda(pCafeParam param, double* parameters)
{
	int i,k;

	if (param->parameters[0] != parameters[0]) memcpy(param->parameters, parameters, param->num_params*sizeof(double));
	// set lambda
	param->lambda = param->parameters;
	// set k_weights
	if (param->parameterized_k_value > 0) {
		double sumofweights = 0;
		for (i = 0; i < (param->parameterized_k_value-1); i++) {
			param->k_weights[i] = param->parameters[param->num_lambdas*(param->parameterized_k_value-param->fixcluster0)+i];
			sumofweights += param->k_weights[i];
		}
		param->k_weights[i] = 1 - sumofweights;
		if( param->p_z_membership == NULL) {
			param->p_z_membership = (double**) memory_new_2dim(param->pfamily->flist->size,param->num_lambdas*param->parameterized_k_value,sizeof(double));
			// assign based on param->k_weights (prior)
			for ( i = 0 ; i < param->pfamily->flist->size ; i++ )
			{
				for (k=0; k<param->parameterized_k_value; k++) {
					param->p_z_membership[i][k] = param->k_weights[k];
				}
			}
		}
	}

	
	param->pcafe->k = param->parameterized_k_value;
	pArrayList nlist = param->pcafe->super.nlist;
	pTree tlambda = param->lambda_tree;
	if ( tlambda == NULL )
	{
		for (i = 0; i < nlist->size; i++)
		{
			pCafeNode pcnode = (pCafeNode)nlist->array[i];
			set_birth_death_probabilities3(&pcnode->birth_death_probabilities, param->parameterized_k_value, param->num_lambdas, param->fixcluster0, parameters);
			if (param->parameterized_k_value > 0) {
				reset_k_likelihoods(pcnode, param->parameterized_k_value, param->pcafe->size_of_factor);

				if (pcnode->k_bd) { arraylist_free(pcnode->k_bd, NULL); }
				pcnode->k_bd = arraylist_new(param->parameterized_k_value);
			}
		}
	}
	else
	{
		pArrayList lambda_nlist = tlambda->nlist;		
		for ( i = 0 ; i < nlist->size ; i++ )
		{
			pPhylogenyNode pnode = (pPhylogenyNode)lambda_nlist->array[i];
			pCafeNode pcnode = (pCafeNode)nlist->array[i];

			if (param->parameterized_k_value > 0) {
				reset_k_likelihoods(pcnode, param->parameterized_k_value, param->pcafe->size_of_factor);

				if (pcnode->k_bd) { arraylist_free(pcnode->k_bd, NULL); }
				pcnode->k_bd = arraylist_new(param->parameterized_k_value);
			}
			set_birth_death_probabilities4(&pcnode->birth_death_probabilities, param->parameterized_k_value, param->num_lambdas, param->fixcluster0, pnode->taxaid, param->eqbg, parameters);
		}
	}
}

void cafe_shell_set_lambda_mu(pCafeParam param, double* parameters)
{
	int i,k;
	
	if (param->parameters[0] != parameters[0]) {
		memcpy(param->parameters, parameters, param->num_params*sizeof(double));
	}
	// set lambda and mu
	cafe_param->lambda = cafe_param->parameters;
	if (param->parameterized_k_value > 0) {
		cafe_param->mu = &(cafe_param->parameters[param->num_lambdas*(param->parameterized_k_value-param->fixcluster0)]);
	}
	else {
		cafe_param->mu = &(cafe_param->parameters[param->num_lambdas]);
	}
	// set k_weights
	if (param->parameterized_k_value > 0) {
		double sumofweights = 0;
		for (i = 0; i < (param->parameterized_k_value-1); i++) {
			param->k_weights[i] = param->parameters[param->num_lambdas*(param->parameterized_k_value-param->fixcluster0)+(param->num_mus-param->eqbg)*(param->parameterized_k_value-param->fixcluster0)+i];
			sumofweights += param->k_weights[i];
		}
		param->k_weights[i] = 1 - sumofweights;
		if( param->p_z_membership == NULL) {
			param->p_z_membership = (double**) memory_new_2dim(param->pfamily->flist->size,param->num_lambdas*param->parameterized_k_value,sizeof(double));
			// assign based on param->k_weights (prior)
			for ( i = 0 ; i < param->pfamily->flist->size ; i++ )
			{
				for (k=0; k<param->parameterized_k_value; k++) {
					param->p_z_membership[i][k] = param->k_weights[k];
				}
			}
		}
	}
	
	param->pcafe->k = param->parameterized_k_value;
	pArrayList nlist = param->pcafe->super.nlist;
	pTree tlambda = param->lambda_tree;
	if ( tlambda == NULL )
	{
		for ( i = 0 ; i < nlist->size ; i++ )
		{
			pCafeNode pcnode = (pCafeNode)nlist->array[i];
			set_birth_death_probabilities(&pcnode->birth_death_probabilities, param->parameterized_k_value, param->num_lambdas, param->fixcluster0, parameters);
			if (param->parameterized_k_value > 0) {
				
				reset_k_likelihoods(pcnode, param->parameterized_k_value, param->pcafe->size_of_factor);

				if (pcnode->k_bd) { arraylist_free(pcnode->k_bd, NULL); }
				pcnode->k_bd = arraylist_new(param->parameterized_k_value);
			}
		}
	}
	else
	{
		pArrayList lambda_nlist = tlambda->nlist;		
		for ( i = 0 ; i < nlist->size ; i++ )
		{
			pPhylogenyNode pnode = (pPhylogenyNode)lambda_nlist->array[i];
			pCafeNode pcnode = (pCafeNode)nlist->array[i];
			
			set_birth_death_probabilities2(&pcnode->birth_death_probabilities, param->parameterized_k_value, param->num_lambdas, param->fixcluster0, pnode->taxaid, param->eqbg, parameters);
				
			reset_k_likelihoods(pcnode, param->parameterized_k_value, param->pcafe->size_of_factor);

			if (pcnode->k_bd) { arraylist_free(pcnode->k_bd, NULL); }
			pcnode->k_bd = arraylist_new(param->parameterized_k_value);
		}
	}
}

int cafe_shell_set_familysize()
{
	int i;
	int max = 0;
	char buf[STRING_STEP_SIZE];

	STDERR_IF( cafe_param->pcafe == NULL, "You did not specify tree: command 'tree'\n" );

	pArrayList nlist = cafe_param->pcafe->super.nlist;
	for ( i = 0; i < nlist->size ; i+=2 )
	{
		pCafeNode pnode = (pCafeNode)nlist->array[i];
		sprintf(buf, "%s: ", pnode->super.name );
		int size = -1;
		cafe_shell_prompt( buf , "%d", &size  );
		if ( size < 0 )
		{ 
			fprintf( stderr, "ERROR: You put wrong data, you must enter an integer greater than or equal to 0\n");
			cafe_shell_prompt( "Retry? [Y|N] ", "%s", buf);
			if ( buf[0] != 'Y' && buf[0] != 'y' ) return -1; 
			i -= 2;
		}
		else
		{
			pnode->familysize = size;
			if ( size > max ) max = size;
		}
	}
	return max;
}

int cafe_shell_set_branchlength()
{
	int i;
	char buf[STRING_STEP_SIZE];

	pArrayList nlist = cafe_param->pcafe->super.nlist;
	for ( i = 0; i < nlist->size ; i++ )
	{
		pPhylogenyNode pnode = (pPhylogenyNode)nlist->array[i];
		if ( tree_is_root( (pTree)cafe_param->pcafe, (pTreeNode)pnode) ) continue;
		printf("%d[%d]: ", i, (int)pnode->branchlength );
		if (fgets(buf,STRING_STEP_SIZE,stdin) == NULL)
			fprintf(stderr, "Failed to read input\n");

		size_t len = strlen(buf);
		buf[--len] = '\0';
		if ( len != 0 )
		{
			int size = -1;
			sscanf( buf, "%d", &size );
			if ( size > 0 )
			{ 
				pnode->branchlength = size;
			}
			else
			{
				fprintf(stderr,"ERROR: the branch length of node %d is not changed\n", i);
			}
		}
	}
	if (probability_cache) cafe_tree_set_birthdeath(cafe_param->pcafe);
	return 0;
}


/**
* \brief Initializes the global \ref cafe_param that holds the data acted upon by cafe. Called at program startup.
* 
*/
void cafe_shell_init(int quiet)
{
	cafe_param = (pCafeParam)memory_new(1,sizeof(CafeParam));
	cafe_param->family_size.root_min = 1;
	cafe_param->family_size.root_max = 1;
	cafe_param->family_size.min = 0;
	cafe_param->family_size.max = 1;
	cafe_param->param_set_func = cafe_shell_set_lambda;
	cafe_param->flog = stdout;
	cafe_param->num_threads = 1;
	cafe_param->num_random_samples = 1000;
	cafe_param->pvalue = 0.01;
	cafe_param->quiet = quiet;
}

void phylogeny_lambda_parse_func(pTree ptree, pTreeNode ptnode)
{
	pPhylogenyNode pnode = (pPhylogenyNode)ptnode;
	if (pnode->name) {
		sscanf( pnode->name, "%d", &pnode->taxaid );	
		cafe_param->pcafe->branch_params_cnt++;
	}
	pnode->taxaid--;
}

int __cafe_cmd_lambda_tree(pArgument parg)
{
	int idx = 1;
	pTree ptree;
	char* plambdastr = NULL;
	cafe_param->pcafe->branch_params_cnt = 0;
	if ( parg->argc == 2 )
	{
		sscanf( parg->argv[0], "%d", &idx );
		plambdastr = parg->argv[1];
		ptree = phylogeny_load_from_string(parg->argv[1], tree_new, phylogeny_new_empty_node, phylogeny_lambda_parse_func, 0 );
	}
	else
	{
		plambdastr = parg->argv[0];
		ptree = phylogeny_load_from_string(parg->argv[0], tree_new, phylogeny_new_empty_node, phylogeny_lambda_parse_func, 0 );
	}
	tree_build_node_list(ptree);
	if ( ptree->nlist->size != cafe_param->pcafe->super.nlist->size )
	{
		fprintf(stderr, "Lambda has a different topology from the tree\n");
		return -1;
	}
	if (cafe_param->pcafe->branch_params_cnt != cafe_param->pcafe->super.nlist->size-1) {
		fprintf(stderr,"ERROR(lambda -t): Branch lambda classes not totally specified.\n");
		fprintf(stderr,"%s\n", plambdastr);
		fprintf(stderr,"You have to specify lambda classes for all branches including the internal branches of the tree.\n");
		fprintf(stderr,"There are total %d branches in the tree.\n", cafe_param->pcafe->super.nlist->size-1);	// branch_cnt = node_cnt - 1 
		return -1;
	}

	if ( idx == 2 ) 
	{
		if ( tmp_lambda_tree ) phylogeny_free(tmp_lambda_tree);
		tmp_lambda_tree = ptree;
		return 1;
	}
	else 
	{
		if ( cafe_param->lambda_tree ) phylogeny_free(cafe_param->lambda_tree);
		cafe_param->lambda_tree = ptree;
		int l, m, n;
		pArrayList nlist = (pArrayList)cafe_param->lambda_tree->nlist;
		memset( cafe_param->old_branchlength, 0, sizeof(int) * cafe_param->num_branches );	// temporarily use space for old_branchlength 
		for ( l = m = 0 ; l < nlist->size ; l++ )
		{
			int lambda_idx= ((pPhylogenyNode)nlist->array[l])->taxaid;		// lambda tree parameter specification is saved in taxaid
			if ( lambda_idx < 0 ) continue;
			for ( n = 0 ; n < m ; n++ )
			{
				if ( cafe_param->old_branchlength[n] == lambda_idx ) break;	// find existing lambda idx
			}
			if ( n == m ) cafe_param->old_branchlength[m++] = lambda_idx;	// save new lambda idx
		}
		cafe_param->num_lambdas = m;										// number of branch-specific lambdas = m
		if (!cafe_param->quiet)
			printf("The number of lambdas is %d\n", m );
	}
	return 0;
}


int cafe_cmd_lambda_mu(int argc, char* argv[])
{
	int i,j;
	STDERR_IF( cafe_param->pfamily == NULL, "ERROR(lambdamu): You must load family data first: command 'load'\n");
	STDERR_IF( cafe_param->pcafe == NULL, "ERROR(lambdamu): You did not specify tree: command 'tree'\n" );
	
	
	pCafeTree pcafe = cafe_param->pcafe;
	pArrayList pargs = cafe_shell_build_argument(argc,argv);
	cafe_param->lambda = NULL;
	cafe_param->mu = NULL;
	if (cafe_param->lambda_tree) {
		phylogeny_free(cafe_param->lambda_tree);
		cafe_param->lambda_tree = NULL;
	}
	if ( cafe_param->mu_tree ) 
	{
		phylogeny_free(cafe_param->mu_tree);
		cafe_param->mu_tree = NULL;
	}
	cafe_param->num_lambdas = -1;
	cafe_param->num_mus = -1;
	cafe_param->parameterized_k_value = 0;
	cafe_param->param_set_func = cafe_shell_set_lambda_mu;
	
	int bdone = 0;
	int bsearch = 0;
	int bprint = 0;
	
	//////
	CafeParam tmpparam;
	pCafeParam tmp_param = &tmpparam;
	memset(tmp_param, 0, sizeof(CafeParam));
    tmp_param->posterior = 1;			
	STDERR_IF( ( cafe_param->pfamily == NULL || cafe_param->pcafe == NULL ), "ERROR(lambda): Please load family (\"load\") and cafe tree (\"tree\") before running \"lambda\" command.");
	
	for ( i = 0 ; i < pargs->size ; i++ )
	{
		pArgument parg = (pArgument)pargs->array[i];
		
		// Search for whole family 
		if ( !strcmp( parg->opt, "-s" ) )
		{
			bsearch = 1;
		}
		else if ( !strcmp( parg->opt, "-checkconv" ) )
		{
			tmp_param->checkconv = 1;
		}		
		else if ( !strcmp( parg->opt, "-t") )
		{
			bdone = __cafe_cmd_lambda_tree(parg);
			if (bdone < 0) {
				return -1;
			}
			pString pstr = phylogeny_string(cafe_param->lambda_tree,NULL);
			cafe_log(cafe_param,"Lambda Tree: %s\n", pstr->buf);
			string_free(pstr);
			tmp_param->lambda_tree = cafe_param->lambda_tree;
			tmp_param->num_lambdas = cafe_param->num_lambdas;
			tmp_param->num_mus = cafe_param->num_mus = cafe_param->num_lambdas;
		}
		else if ( !strcmp( parg->opt, "-l") )
		{
			if ( tmp_param->lambda ) memory_free(tmp_param->lambda);
			tmp_param->lambda = NULL;
			tmp_param->lambda = (double*)memory_new(parg->argc, sizeof(double) );
			for ( j = 0 ; j < parg->argc; j++ )
			{
			 	sscanf( parg->argv[j], "%lf", &tmp_param->lambda[j] );
			}
			tmp_param->num_params += parg->argc;
		}
		else if ( !strcmp( parg->opt, "-m") )
		{
			if ( tmp_param->mu ) memory_free(tmp_param->mu);
			tmp_param->mu = NULL;
			tmp_param->mu = (double*)memory_new(parg->argc, sizeof(double) );
			for ( j = 0 ; j < parg->argc; j++ )
			{
			 	sscanf( parg->argv[j], "%lf", &tmp_param->mu[j] );
			}
			tmp_param->num_params += parg->argc;
		}
		else if ( !strcmp( parg->opt, "-p") )
		{
			if ( tmp_param->k_weights ) memory_free(tmp_param->k_weights);
			tmp_param->k_weights = NULL;
			tmp_param->k_weights = (double*)memory_new(parg->argc, sizeof(double) );
			for ( j = 0 ; j < parg->argc; j++ )
			{
				sscanf( parg->argv[j], "%lf", &tmp_param->k_weights[j] );
			}
			tmp_param->num_params += parg->argc;
		}
		else if ( !strcmp (parg->opt, "-k") ) 
		{
			sscanf( parg->argv[0], "%d", &tmp_param->parameterized_k_value );	
		}
		else if ( !strcmp (parg->opt, "-f") ) 
		{
			tmp_param->fixcluster0 = 1;
		}
		else if ( !strcmp( parg->opt, "-eqbg") ) 
		{
			tmp_param->eqbg = 1;
		}
	}
		
	//////////
	arraylist_free( pargs, free );
	
	if ( bdone ) 
	{
		if ( bdone ) return 0;
	}
	
	// copy parameters collected to cafe_param based on the combination of options.
	{
		cafe_param->posterior = tmp_param->posterior;
		if (cafe_param->posterior) {
			// set rootsize prior based on leaf size
			cafe_set_prior_rfsize_empirical(cafe_param);
		}		
		// search or set
		if (bsearch) {
            // prepare parameters
			if (tmp_param->lambda_tree != NULL) {
				// cafe_param->num_lambdas determined by lambda tree.
				cafe_param->eqbg = tmp_param->eqbg;
				if (tmp_param->parameterized_k_value > 0) {
					cafe_param->parameterized_k_value = tmp_param->parameterized_k_value;
					cafe_param->fixcluster0 = tmp_param->fixcluster0;
					cafe_param->num_params = (tmp_param->num_lambdas*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))+
					((tmp_param->num_mus-tmp_param->eqbg)*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))+
					(tmp_param->parameterized_k_value-1);
					
					if( cafe_param->parameters ) memory_free(cafe_param->parameters);
					cafe_param->parameters = NULL;
					cafe_param->parameters = (double*)memory_new(cafe_param->num_params, sizeof(double));
					if (cafe_param->k_weights) { memory_free(cafe_param->k_weights);}
					cafe_param->k_weights = NULL;
					cafe_param->k_weights = (double*) memory_new(cafe_param->parameterized_k_value, sizeof(double));
				}
				else {	// search whole dataset branch specific
					cafe_param->num_params = tmp_param->num_lambdas+(tmp_param->num_mus-tmp_param->eqbg);
					
					if( cafe_param->parameters ) memory_free(cafe_param->parameters);
					cafe_param->parameters = NULL;
					cafe_param->parameters = (double*)memory_new(cafe_param->num_params, sizeof(double));
				}
			}
			else {
				cafe_param->num_lambdas = tmp_param->num_lambdas = 1;	
				cafe_param->num_mus = tmp_param->num_mus = 1;	
				if (tmp_param->eqbg) {
					fprintf( stderr, "ERROR(lambdamu): Cannot use option eqbg without specifying a lambda tree. \n");
					return -1;											
				}
				if (tmp_param->parameterized_k_value > 0) {
					cafe_param->parameterized_k_value = tmp_param->parameterized_k_value;
					cafe_param->fixcluster0 = tmp_param->fixcluster0;
					cafe_param->num_params = (tmp_param->num_lambdas*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))+
					(tmp_param->num_mus*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))+
					(tmp_param->parameterized_k_value-1);
					
					if( cafe_param->parameters ) memory_free(cafe_param->parameters);
					cafe_param->parameters = NULL;
					cafe_param->parameters = (double*)memory_new(cafe_param->num_params, sizeof(double));
					if (cafe_param->k_weights) { memory_free(cafe_param->k_weights);}
					cafe_param->k_weights = NULL;
					cafe_param->k_weights = (double*) memory_new(cafe_param->parameterized_k_value, sizeof(double));
				}
				else {	// search whole dataset whole tree
					cafe_param->num_params = tmp_param->num_lambdas+tmp_param->num_mus;
					
					if( cafe_param->parameters ) memory_free(cafe_param->parameters);
					cafe_param->parameters = NULL;
					cafe_param->parameters = (double*)memory_new(cafe_param->num_params, sizeof(double));
				}
			}
			// search
			if (tmp_param->checkconv) { cafe_param->checkconv = 1; }
			cafe_best_lambda_mu_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->num_mus, cafe_param->parameterized_k_value);
		}
		else {
			if (tmp_param->lambda_tree != NULL) {
				// cafe_param->num_lambdas determined by lambda tree.
				cafe_param->eqbg = tmp_param->eqbg;
				if (tmp_param->parameterized_k_value > 0) {	// search clustered branch specific
					cafe_param->parameterized_k_value = tmp_param->parameterized_k_value;
					cafe_param->fixcluster0 = tmp_param->fixcluster0;
					cafe_param->num_params = (tmp_param->num_lambdas*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))+
					((tmp_param->num_mus-tmp_param->eqbg)*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))+
					(tmp_param->parameterized_k_value-1);
					
					// check if the numbers of lambdas and proportions put in matches the number of parameters
					if (cafe_param->num_params != tmp_param->num_params) {
						fprintf( stderr, "ERROR(lambdamu): Number of parameters not correct. \n");
						fprintf( stderr, "the number of -l lambdas -m mus and -p proportions are %d they need to be %d\n", tmp_param->num_params, cafe_param->num_params );
						pString pstr = phylogeny_string(tmp_param->lambda_tree,NULL);
						fprintf( stderr, "based on the tree %s and -k clusters %d.\n", pstr->buf, cafe_param->parameterized_k_value );
						string_free(pstr);
						return -1;						
					}
					
					// copy user input into parameters
					if( cafe_param->parameters ) memory_free(cafe_param->parameters);
					cafe_param->parameters = NULL;
					cafe_param->parameters = (double*)memory_new(cafe_param->num_params, sizeof(double));
					memcpy(cafe_param->parameters,tmp_param->lambda, sizeof(double)*tmp_param->num_lambdas*(tmp_param->parameterized_k_value-tmp_param->fixcluster0));
					memcpy(&cafe_param->parameters[cafe_param->num_lambdas*(cafe_param->parameterized_k_value-tmp_param->fixcluster0)],tmp_param->mu, sizeof(double)*((tmp_param->num_mus-tmp_param->eqbg)*(tmp_param->parameterized_k_value-tmp_param->fixcluster0)));
					memcpy(&cafe_param->parameters[(cafe_param->num_lambdas*(cafe_param->parameterized_k_value-tmp_param->fixcluster0))+((tmp_param->num_mus-tmp_param->eqbg)*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))], tmp_param->k_weights, sizeof(double)*(tmp_param->parameterized_k_value-1));
					// prepare space for k_weights
					if ( cafe_param->k_weights ) memory_free(cafe_param->k_weights);
					cafe_param->k_weights = NULL;
					cafe_param->k_weights = (double*)memory_new(cafe_param->parameterized_k_value-1, sizeof(double) );										
				}
				else {	// search whole dataset branch specific
					cafe_param->num_params = tmp_param->num_lambdas+(tmp_param->num_mus-tmp_param->eqbg);
					
					// check if the numbers of lambdas and proportions put in matches the number of parameters
					if (cafe_param->num_params != tmp_param->num_params) {
						fprintf( stderr, "ERROR(lambdamu): Number of parameters not correct. \n");
						fprintf( stderr, "the number of -l lambdas -m mus are %d they need to be %d\n", tmp_param->num_params, cafe_param->num_params );
						pString pstr = phylogeny_string(tmp_param->lambda_tree,NULL);
						fprintf( stderr, "based on the tree %s \n", pstr->buf );
						string_free(pstr);
						return -1;						
					}
					
					// copy user input into parameters
					if( cafe_param->parameters ) memory_free(cafe_param->parameters);
					cafe_param->parameters = NULL;
					cafe_param->parameters = (double*)memory_new(cafe_param->num_params, sizeof(double));
					memcpy(cafe_param->parameters,tmp_param->lambda, sizeof(double)*tmp_param->num_lambdas);
					memcpy(&cafe_param->parameters[cafe_param->num_lambdas],tmp_param->mu, sizeof(double)*(tmp_param->num_mus-tmp_param->eqbg));
					
				}
			}
			else {
				cafe_param->num_lambdas = tmp_param->num_lambdas = 1;	
				cafe_param->num_mus = tmp_param->num_mus = 1;	
				if (tmp_param->eqbg) {
					fprintf( stderr, "ERROR(lambdamu): Cannot use option eqbg without specifying a lambda tree. \n");
					return -1;											
				}
				if (tmp_param->parameterized_k_value > 0) {				// search clustered whole tree
					cafe_param->parameterized_k_value = tmp_param->parameterized_k_value;
					cafe_param->fixcluster0 = tmp_param->fixcluster0;
					cafe_param->num_params = (tmp_param->num_lambdas*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))+
					(tmp_param->num_mus*(tmp_param->parameterized_k_value-tmp_param->fixcluster0))+
					(tmp_param->parameterized_k_value-1);
					
					// check if the numbers of lambdas and proportions put in matches the number of parameters
					if (cafe_param->num_params != tmp_param->num_params) {
						fprintf( stderr, "ERROR(lambdamu): Number of parameters not correct. \n");
						fprintf( stderr, "the number of -l lambdas -m mus and -p proportions are %d they need to be %d\n", tmp_param->num_params, cafe_param->num_params );
						fprintf( stderr, "based on the -k clusters %d.\n", cafe_param->parameterized_k_value );
						return -1;						
					}
					
					// copy user input into parameters
					if( cafe_param->parameters ) memory_free(cafe_param->parameters);
					cafe_param->parameters = NULL;
					cafe_param->parameters = (double*)memory_new(cafe_param->num_params, sizeof(double));
					memcpy(cafe_param->parameters,tmp_param->lambda, sizeof(double)*tmp_param->num_lambdas*(tmp_param->parameterized_k_value-tmp_param->fixcluster0));
					memcpy(&cafe_param->parameters[cafe_param->num_lambdas*(cafe_param->parameterized_k_value-tmp_param->fixcluster0)], tmp_param->mu, sizeof(double)*tmp_param->num_mus*(cafe_param->parameterized_k_value-tmp_param->fixcluster0));
					memcpy(&cafe_param->parameters[cafe_param->num_lambdas*(cafe_param->parameterized_k_value-tmp_param->fixcluster0)+tmp_param->num_mus*(cafe_param->parameterized_k_value-tmp_param->fixcluster0)], tmp_param->k_weights, sizeof(double)*(tmp_param->parameterized_k_value-1));
					// prepare space for k_weights
					if ( cafe_param->k_weights ) memory_free(cafe_param->k_weights);
					cafe_param->k_weights = NULL;
					cafe_param->k_weights = (double*)memory_new(cafe_param->parameterized_k_value-1, sizeof(double) );										
					
				}
				else {	// search whole dataset whole tree
					cafe_param->num_params = tmp_param->num_lambdas+tmp_param->num_mus;
					
					// check if the numbers of lambdas and proportions put in matches the number of parameters
					if (cafe_param->num_params != tmp_param->num_params) {
						fprintf( stderr, "ERROR(lambdamu): Number of parameters not correct. \n");
						fprintf( stderr, "the number of -l lambdas -m mus are %d they need to be %d\n", tmp_param->num_params, cafe_param->num_params );
						return -1;						
					}
					
					// copy user input into parameters
					if( cafe_param->parameters ) memory_free(cafe_param->parameters);
					cafe_param->parameters = NULL;
					cafe_param->parameters = (double*)memory_new(cafe_param->num_params, sizeof(double));
					memcpy(cafe_param->parameters,tmp_param->lambda, sizeof(double)*tmp_param->num_lambdas);
					memcpy(&cafe_param->parameters[tmp_param->num_lambdas],tmp_param->mu, sizeof(double)*tmp_param->num_mus);
				}
			}
			cafe_param->param_set_func(cafe_param, cafe_param->parameters);
		}
	}
	
	
	//////////
	
	
	if (bprint)
	{
		pString pstr = cafe_tree_string_with_lambda(pcafe);
		printf("%s\n", pstr->buf);
		string_free(pstr);
	}
	if (cafe_param->pfamily)
	{
		reset_birthdeath_cache(cafe_param->pcafe, cafe_param->parameterized_k_value, &cafe_param->family_size);
	}
	   
	cafe_log(cafe_param,"DONE: Lamda,Mu Search or setting, for command:\n");
	char buf[STRING_STEP_SIZE];
	buf[0] = '\0';
	string_pchar_join(buf," ",argc, argv);
	cafe_log(cafe_param,"%s\n", buf);
	
	if (bsearch && (cafe_param->parameterized_k_value > 0)) {
		// print the cluster memberships
		cafe_family_print_cluster_membership(cafe_param);
	}
	return 0;
}






void __cafe_cmd_viterbi_family_print(int idx)
{
	pCafeTree pcafe = cafe_param->pcafe;
	cafe_family_set_size_with_family_forced(cafe_param->pfamily,idx,pcafe);
	compute_tree_likelihoods(pcafe);
	int ridx =  __maxidx(((pCafeNode)pcafe->super.root)->likelihoods,pcafe->rfsize) + pcafe->rootfamilysizes[0];
	double mlh =  __max( ((pCafeNode)pcafe->super.root)->likelihoods,pcafe->rfsize);
	//compute_tree_likelihoods(pcafe);
	cafe_tree_viterbi(pcafe);
	pString pstr = cafe_tree_string(pcafe);
	printf("%g(%d)\t%s\n", mlh , ridx,  pstr->buf );
	string_free(pstr);
}





double _cafe_cross_validate_by_family(char* queryfile, char* truthfile, char* errortype) 
{
	int i, j;
	double MSE = 0;
	double MAE = 0;
	double SSE = 0;
	double SAE = 0;
	cafe_family_read_query_family(cafe_param, queryfile);
	if ( cafe_param->cv_test_count_list == NULL ) return -1;
	
	// read in validation data
	pCafeFamily truthfamily = cafe_family_new( truthfile, 1 );
	if ( truthfamily == NULL ) {
		fprintf(stderr, "failed to read in true values %s\n", truthfile);
		return -1;
	}
	
	// now compare reconstructed count to true count	
	pCafeTree pcafe = cafe_param->pcafe;
	pCafeTree truthtree = cafe_tree_copy(pcafe);
	// set parameters
	if ( truthtree )
	{
		cafe_family_set_species_index(truthfamily, truthtree);
	}

	reset_birthdeath_cache(cafe_param->pcafe, cafe_param->parameterized_k_value, &cafe_param->family_size);
	
	for(i=0; i< cafe_param->cv_test_count_list->size; i++) 
	{
		int* testcnt = (int*)cafe_param->cv_test_count_list->array[i];
		cafe_family_set_size(truthfamily, i, truthtree);
		cafe_family_set_size_by_species(cafe_param->cv_test_species_list->array[i], *testcnt, pcafe);
		if (cafe_param->posterior) {
			cafe_tree_viterbi_posterior(pcafe, cafe_param);
		}
		else {
			cafe_tree_viterbi(pcafe);
		}
		// leaf nodes SSE
		SSE = 0;
		SAE = 0;
		int nodecnt = 0;
		for (j=0; j<pcafe->super.nlist->size; j=j+2) {
			int error = ((pCafeNode)truthtree->super.nlist->array[j])->familysize-((pCafeNode)pcafe->super.nlist->array[j])->familysize;
			SSE += pow(error, 2);
			SAE += abs(error);
			nodecnt++;
		}
		MSE += SSE/nodecnt;
		MSE += SAE/nodecnt;
	}
	cafe_free_birthdeath_cache(pcafe);

	MSE = MSE/cafe_param->cv_test_count_list->size;
	MAE = MAE/cafe_param->cv_test_count_list->size;
	cafe_log( cafe_param, "MSE %f\n", MSE );
	cafe_log( cafe_param, "MAE %f\n", MSE );

	double returnerror = -1;
	if (strncmp(errortype, "MSE", 3)==0) {
		returnerror = MSE;
	}
	else if (strncmp(errortype, "MAE", 3)==0) {
		returnerror = MAE;
	}
	return returnerror;
}




double _cafe_cross_validate_by_species(char* validatefile, char* errortype) 
{
	int i, j;
	cafe_family_read_validate_species( cafe_param, validatefile );
	if ( cafe_param->cv_test_count_list == NULL ) return -1;
	// now compare reconstructed count to true count	
	pCafeTree pcafe = cafe_param->pcafe;

	reset_birthdeath_cache(cafe_param->pcafe, cafe_param->parameterized_k_value, &cafe_param->family_size);
	pArrayList estimate_size = arraylist_new(cafe_param->cv_test_count_list->size);
	for(i=0; i< cafe_param->pfamily->flist->size; i++) 
	{
		cafe_family_set_size(cafe_param->pfamily,i, pcafe);
		if (cafe_param->posterior) {
			cafe_tree_viterbi_posterior(pcafe, cafe_param);
		}
		else {
			cafe_tree_viterbi(pcafe);
		}
		for (j=0; j<pcafe->super.nlist->size; j++) {
			char* nodename = ((pPhylogenyNode)pcafe->super.nlist->array[j])->name;
			if (nodename && (strcmp(nodename, cafe_param->cv_species_name)==0)) {
				int* pFamilysize = memory_new(1, sizeof(int));
				*pFamilysize = ((pCafeNode)pcafe->super.nlist->array[j])->familysize;
				arraylist_add(estimate_size, (void*)pFamilysize);
			}
			
		}
	}
	cafe_free_birthdeath_cache(pcafe);
	double MSE = 0;
	double MAE = 0;
	STDERR_IF(cafe_param->cv_test_count_list->size != cafe_param->pfamily->flist->size, "list size don't match\n");
	for(i=0; i<cafe_param->cv_test_count_list->size; i++) {
		int error = (*((int*)cafe_param->cv_test_count_list->array[i]) - *((int*)estimate_size->array[i]));
		MSE += pow(error, 2);
		MAE += abs(error);
	}
	MSE = MSE/(cafe_param->cv_test_count_list->size);
	MAE = MAE/(cafe_param->cv_test_count_list->size);
	cafe_log( cafe_param, "MSE %f\n", MSE );
	cafe_log( cafe_param, "MAE %f\n", MAE );
	
	arraylist_free(estimate_size, free);
	
	double returnerror = -1;
	if (strncmp(errortype, "MSE", 3)==0) {
		returnerror = MSE;
	}
	else if (strncmp(errortype, "MAE", 3)==0) {
		returnerror = MAE;
	}
	return returnerror;
}

void set_range_from_family(family_size_range* range, pCafeFamily family)
{
	init_family_size(range, family->max_size);
}

int cafe_cmd_crossvalidation_by_family(int argc, char* argv[])
{
	int i;
	STDERR_IF( cafe_param->pfamily == NULL, "ERROR(cvfamily): You did not load family: command 'load'\n" );
	STDERR_IF( cafe_param->pcafe == NULL, "ERROR(cvfamily): You did not specify tree: command 'tree'\n" );
	STDERR_IF( cafe_param->lambda == NULL, "ERROR(cvfamily): You did not set the parameters: command 'lambda' or 'lambdamu'\n" );
	
	double MSE_allfolds = 0;
	pCafeFamily pcafe_original = cafe_param->pfamily;
	
	if ( argc < 2 )
	{
		fprintf(stderr, "Usage(cvfamily): %s -fold <num>\n", argv[0] );
		return -1;
	}
	pArrayList pargs = cafe_shell_build_argument(argc, argv);
	pArgument parg;
	int cv_fold = 0;
	if ((parg = cafe_shell_get_argument("-fold", pargs)))
	{
		sscanf( parg->argv[0], "%d", &cv_fold );
	}
	// set up the training-validation set
	cafe_family_split_cvfiles_byfamily(cafe_param, cv_fold);
	
	//
	for (i=0; i<cv_fold; i++) 
	{
		char trainfile[STRING_BUF_SIZE];
		char queryfile[STRING_BUF_SIZE];
		char validatefile[STRING_BUF_SIZE];
		sprintf(trainfile, "%s.%d.train", cafe_param->str_fdata->buf, i+1);
		sprintf(queryfile, "%s.%d.query", cafe_param->str_fdata->buf, i+1);
		sprintf(validatefile, "%s.%d.valid", cafe_param->str_fdata->buf, i+1);
		
		// read in training data
		pCafeFamily tmpfamily = cafe_family_new( trainfile, 1 );
		if ( tmpfamily == NULL ) {
			fprintf(stderr, "failed to read in training data %s\n", trainfile);
			return -1;
		}
		cafe_param->pfamily = tmpfamily;
		
		set_range_from_family(&cafe_param->family_size, cafe_param->pfamily);
		if ( cafe_param->pcafe )
		{
			cafe_tree_set_parameters(cafe_param->pcafe, &cafe_param->family_size, 0 );
			cafe_family_set_species_index(cafe_param->pfamily, cafe_param->pcafe);
		}
		// re-train 
		if (cafe_param->num_mus > 0) {
			cafe_best_lambda_mu_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->num_mus, cafe_param->parameterized_k_value);
		}
		else {
			cafe_best_lambda_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->parameterized_k_value);
		}
		
		//cross-validate
		double MSE = _cafe_cross_validate_by_family(queryfile, validatefile, "MSE");
		MSE_allfolds += MSE;
		cafe_log( cafe_param, "MSE fold %d %f\n", i+1, MSE );
		
		cafe_family_free(tmpfamily);
	}
	MSE_allfolds = MSE_allfolds/cv_fold;
	cafe_log( cafe_param, "MSE all folds %f\n", MSE_allfolds);
	
	//re-load the original family file
	cafe_param->pfamily = pcafe_original;
	set_range_from_family(&cafe_param->family_size, cafe_param->pfamily);

	if ( cafe_param->pcafe )
	{
		cafe_tree_set_parameters(cafe_param->pcafe, &cafe_param->family_size, 0 );
		cafe_family_set_species_index(cafe_param->pfamily, cafe_param->pcafe);
	}
	// re-train 
	if (cafe_param->num_mus > 0) {
		cafe_best_lambda_mu_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->num_mus, cafe_param->parameterized_k_value);
	}
	else {
		cafe_best_lambda_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->parameterized_k_value);
	}
	
	// remove training-validation set
	cafe_family_clean_cvfiles_byfamily(cafe_param, cv_fold);	
	return 0;
}


int cafe_cmd_crossvalidation_by_species(int argc, char* argv[])
{
	int i;
	STDERR_IF( cafe_param->pfamily == NULL, "ERROR(cvspecies): You did not load family: command 'load'\n" );
	STDERR_IF( cafe_param->pcafe == NULL, "ERROR(cvspecies): You did not specify tree: command 'tree'\n" );
	STDERR_IF( cafe_param->lambda == NULL, "ERROR(cvspecies): You did not set the parameters: command 'lambda' or 'lambdamu'\n" );
	
	double MSE_allspecies = 0;
	pCafeFamily pcafe_original = cafe_param->pfamily;
	int num_species_original = cafe_param->pfamily->num_species;
	char** species_names_original = cafe_param->pfamily->species;
	
	if ( argc < 2 )
	{
		// set up the training-validation set
		cafe_family_split_cvfiles_byspecies(cafe_param);
		
		for (i=0; i<num_species_original; i++) {
			char trainfile[STRING_BUF_SIZE];
			char validatefile[STRING_BUF_SIZE];
			sprintf(trainfile, "%s.%s.train", cafe_param->str_fdata->buf, species_names_original[i]);
			sprintf(validatefile, "%s.%s.valid", cafe_param->str_fdata->buf, species_names_original[i]);
			
			// read in training data
			pCafeFamily tmpfamily = cafe_family_new( trainfile, 1 );
			if ( tmpfamily == NULL ) {
				fprintf(stderr, "failed to read in training data %s\n", trainfile);
				fprintf(stderr, "did you load the family data with the cross-validation option (load -i <familyfile> -cv)?\n");
				return -1;
			}
			cafe_param->pfamily = tmpfamily;
			
			set_range_from_family(&cafe_param->family_size, cafe_param->pfamily);

			if ( cafe_param->pcafe )
			{
				cafe_tree_set_parameters(cafe_param->pcafe, &cafe_param->family_size, 0 );
				cafe_family_set_species_index(cafe_param->pfamily, cafe_param->pcafe);
			}
			// re-train 
			if (cafe_param->num_mus > 0) {
				cafe_best_lambda_mu_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->num_mus, cafe_param->parameterized_k_value);
			}
			else {
				cafe_best_lambda_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->parameterized_k_value);
			}
			
			//cross-validate
			double MSE = _cafe_cross_validate_by_species(validatefile, "MSE");
			MSE_allspecies += MSE;
			cafe_log( cafe_param, "MSE %s %f\n", cafe_param->cv_species_name, MSE );
			
			cafe_family_free(tmpfamily);
		}
		MSE_allspecies = MSE_allspecies/num_species_original;
		cafe_log( cafe_param, "MSE all species %f\n", MSE_allspecies );
		
		//re-load the original family file
		cafe_param->pfamily = pcafe_original;
		set_range_from_family(&cafe_param->family_size, cafe_param->pfamily);

		if ( cafe_param->pcafe )
		{
			cafe_tree_set_parameters(cafe_param->pcafe, &cafe_param->family_size, 0 );
			cafe_family_set_species_index(cafe_param->pfamily, cafe_param->pcafe);
		}
		// re-train 
		if (cafe_param->num_mus > 0) {
			cafe_best_lambda_mu_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->num_mus, cafe_param->parameterized_k_value);
		}
		else {
			cafe_best_lambda_by_fminsearch(cafe_param, cafe_param->num_lambdas, cafe_param->parameterized_k_value);
		}
		
		// remove training-validation set
		cafe_family_clean_cvfiles_byspecies(cafe_param);
	}
	else 
	{
		pArrayList pargs = cafe_shell_build_argument(argc, argv);
		pArgument parg;
		pString str_fdata;
		if ((parg = cafe_shell_get_argument("-i", pargs)))
		{
			str_fdata = string_join(" ",parg->argc, parg->argv );
			_cafe_cross_validate_by_species(str_fdata->buf, "MSE"); 
		}
	}		
	return 0;
}



void log_param_values(pCafeParam param)
{
	cafe_log(param, "-----------------------------------------------------------\n");
	cafe_log(param, "Family information: %s\n", param->str_fdata->buf);
	cafe_log(param, "Log: %s\n", param->flog == stdout ? "stdout" : param->str_log->buf);
	if (param->pcafe)
	{
		pString pstr = phylogeny_string((pTree)param->pcafe, NULL);
		cafe_log(param, "Tree: %s\n", pstr->buf);
		string_free(pstr);
	}
	cafe_log(param, "The number of families is %d\n", param->pfamily->flist->size);
	cafe_log(param, "Root Family size : %d ~ %d\n", param->family_size.root_min, param->family_size.root_max);
	cafe_log(param, "Family size : %d ~ %d\n", param->family_size.min, param->family_size.max);
	cafe_log(param, "P-value: %f\n", param->pvalue);
	cafe_log(param, "Num of Threads: %d\n", param->num_threads);
	cafe_log(param, "Num of Random: %d\n", param->num_random_samples);
	if (param->lambda)
	{
		pString pstr = cafe_tree_string_with_lambda(param->pcafe);
		cafe_log(param, "Lambda: %s\n", pstr->buf);
		string_free(pstr);
	}
}

extern double __cafe_best_lambda_search(double* plambda, void* args);
extern double __cafe_best_lambda_mu_search(double* pparams, void* args);
extern double __cafe_cluster_lambda_search(double* plambda, void* args);
extern double __cafe_cluster_lambda_mu_search(double* pparams, void* args);

double cafe_shell_score()
{
	int i=0;
	double score = 0;
	if (cafe_param->parameterized_k_value > 0) {
		if (cafe_param->num_mus > 0) {
			score = -__cafe_cluster_lambda_mu_search(cafe_param->parameters, (void*)cafe_param);
			// print
			char buf[STRING_STEP_SIZE];
			buf[0] = '\0';
			for( i=0; i<cafe_param->num_lambdas; i++) {
				string_pchar_join_double(buf,",", cafe_param->parameterized_k_value, &cafe_param->parameters[i*cafe_param->parameterized_k_value] );
				cafe_log(cafe_param,"Lambda branch %d: %s\n", i, buf);
				buf[0] = '\0';
			}
			for (i=0; i<cafe_param->num_mus; i++) {
				string_pchar_join_double(buf,",", cafe_param->parameterized_k_value, &cafe_param->parameters[cafe_param->num_lambdas*cafe_param->parameterized_k_value+i*cafe_param->parameterized_k_value]);
				cafe_log(cafe_param,"Mu branch %d: %s \n", i, buf);
				buf[0] = '\0';
			}
			if (cafe_param->parameterized_k_value > 0) {
				string_pchar_join_double(buf,",", cafe_param->parameterized_k_value, cafe_param->k_weights );
				cafe_log(cafe_param, "p : %s\n", buf);
			}
			cafe_log(cafe_param, "Score: %f\n", score);
			
		}
		else {  
			score = -__cafe_cluster_lambda_search(cafe_param->parameters, (void*)cafe_param);
			// print
			char buf[STRING_STEP_SIZE];
			buf[0] = '\0';
			string_pchar_join_double(buf,",", cafe_param->num_lambdas*cafe_param->parameterized_k_value, cafe_param->parameters );
			cafe_log(cafe_param,"Lambda : %s\n", buf);
			buf[0] = '\0';
			if (cafe_param->parameterized_k_value > 0) {
				string_pchar_join_double(buf,",", cafe_param->parameterized_k_value, cafe_param->k_weights );
				cafe_log(cafe_param, "p : %s\n", buf);
			}
			cafe_log(cafe_param, "Score: %f\n", score);		
		}
	}
	else {
		if (cafe_param->num_mus > 0) {
			score = -__cafe_best_lambda_mu_search(cafe_param->parameters, (void*)cafe_param);
			// print
			char buf[STRING_STEP_SIZE];
			buf[0] = '\0';
			string_pchar_join_double(buf,",", cafe_param->num_lambdas, cafe_param->parameters );
			cafe_log(cafe_param,"Lambda : %s ", buf, score);
			buf[0] = '\0';
			string_pchar_join_double(buf,",", cafe_param->num_mus, cafe_param->parameters+cafe_param->num_lambdas );
			cafe_log(cafe_param,"Mu : %s & Score: %f\n", buf, score);		
		}
		else {
			score = -__cafe_best_lambda_search(cafe_param->parameters, (void*)cafe_param);
			// print
			char buf[STRING_STEP_SIZE];
			buf[0] = '\0';
			string_pchar_join_double(buf,",", cafe_param->num_lambdas, cafe_param->parameters );
			cafe_log(cafe_param,"Lambda : %s & Score: %f\n", buf, score);		
		}
	}
	return score;
}

int set_log_file(pCafeParam param, const char *log_file)
{
	if ( param->str_log )
	{
		string_free( param->str_log );
		fclose( param->flog );
		param->str_log = NULL;
	}
	if ( !strcmp(log_file, "stdout" ) )
	{
		param->str_log = NULL;
		param->flog = stdout;
	}
	else
	{
		param->str_log = string_new_with_string(log_file);
		if (  ( param->flog = fopen( param->str_log->buf, "a" ) ) == NULL )
		{
			fprintf(stderr, "ERROR(log): Cannot open log file: %s\n", param->str_log->buf );	
			string_free( param->str_log );
			param->flog = stdout;
			return -1;
		}
	}
	return 0;
}

void __cafe_tree_string_gainloss(pString pstr, pPhylogenyNode ptnode)
{
	int familysize =  ((pCafeNode)ptnode)->familysize;
	if ( ptnode->name ) string_fadd( pstr, "%s", ptnode->name);
	string_fadd(pstr,"_%d", familysize );
	pCafeNode parent = (pCafeNode)ptnode->super.parent;
	if ( parent )
	{
		string_fadd(pstr,"<%d>", familysize - parent->familysize );
	}
}

void __cafe_tree_string_sum_gainloss(pString pstr, pPhylogenyNode ptnode)
{
	int familysize =  ((pCafeNode)ptnode)->familysize;
	if ( ptnode->name ) string_fadd( pstr, "%s", ptnode->name);
	pCafeNode pcnode = (pCafeNode)ptnode;
	string_fadd(pstr,"<%d/%d/%d>", pcnode->viterbi[0], pcnode->viterbi[1], familysize );
}

double __cafe_tree_gainloss_mp_annotation(pString pstr, pTreeNode pnode, pMetapostConfig pmc, va_list ap)
{
	pCafeNode pcnode = (pCafeNode)pnode;
    string_add( pstr, ";\n");
	string_fadd( pstr, "label.urt( btex \\small{%d/%d/%d} ", pcnode->viterbi[0], pcnode->viterbi[1], pcnode->familysize, pnode->id );
	string_fadd( pstr, "etex, p[%d]);\n", pnode->id );
	double last = 0;
	if ( pnode->parent )
	{
		string_fadd( pstr, "xpart mid[%d] = xpart(p[%d]);\n", pnode->id, pnode->id );
		string_fadd( pstr, "ypart mid[%d] = (ypart(p[%d])+ypart(p[%d]))/2;\n", pnode->id, pnode->id, pnode->parent->id );
		string_fadd( pstr, "label.rt( btex $l = %g$ ", ((pPhylogenyNode)pnode)->branchlength );
		string_fadd( pstr, "etex, mid[%d]);\n", pnode->id  );
		string_fadd( pstr, "label.rt( btex $\\lambda=%f$ ", pcnode->birth_death_probabilities.lambda );
		last -= 0.15;
		string_fadd( pstr, "etex, mid[%d] + (0,%fu));\n",  pnode->id, last );
	}
	return last;
}

extern double cafe_tree_mp_remark(pString pstr, pTree ptree, pMetapostConfig pmc, va_list ap1);

int __cafe_cmd_extinct_count_zero(pTree pcafe)
{
	int n;
	int cnt_zero = 0;
	tree_clear_reg(pcafe);
	pArrayList nlist = pcafe->nlist;
	pcafe->root->reg = 1;
	for ( n = 0 ; n < nlist->size ; n+=2 )
	{
		pCafeNode pcnode = (pCafeNode)nlist->array[n];
		if ( pcnode->familysize )
		{
			pTreeNode ptnode = (pTreeNode)pcnode;
			while( ptnode )
			{
				ptnode->reg = 1;
				ptnode = ptnode->parent;
			}
		}
	}
	for ( n = 0 ; n < nlist->size ; n++ )
	{
		pTreeNode pnode = (pTreeNode)nlist->array[n];
		if ( pnode->parent == NULL ) continue;
		if ( pnode->reg == 0 &&  pnode->parent->reg == 1 )
		{
			cnt_zero++;
		}
	}
	return cnt_zero;
}

/**
\ingroup Commands
\brief Runs a Monte Carlo simulation against the data and reports the number of extinctions that occurred.

	Arguments: -r range Can be specified as a max or a colon-separated range
	           -t Number of trials to run
*/
int cafe_cmd_sim_extinct(int argc, char* argv[])
{
	STDERR_IF( cafe_param->pcafe == NULL, "ERROR(simextinct): You did not specify tree: command 'tree'\n" );
	STDERR_IF( cafe_param->lambda == NULL, "ERROR(simextinct): You did not set the parameters: command 'lambda' or 'lambdamu'\n" );

	pArrayList pargs = cafe_shell_build_argument(argc, argv);
	pArgument parg;
	int range[2] = { 1, cafe_param->family_size.root_max };
	int num_trials = 10000;

	cafe_log( cafe_param, "Extinction count from Monte Carlo:\n");
	if ( (parg = cafe_shell_get_argument( "-r", pargs) ) )
	{
		if ( index(parg->argv[0], ':' ) )	
		{
			sscanf( parg->argv[0], "%d:%d", &range[0], &range[1] );		
		}
		else
		{
			sscanf( parg->argv[0], "%d", &range[1] );
			range[0] = range[1];
		}
	}
	cafe_log( cafe_param, "root range: %d ~ %d\n", range[0], range[1] );

	if ( (parg = cafe_shell_get_argument( "-t", pargs) ) )
	{
		sscanf( parg->argv[0], "%d", &num_trials);	
	}
	cafe_log( cafe_param, "# trials: %d\n", num_trials );

	if ( range[0] > range[1] || range[1] > cafe_param->family_size.root_max)
	{
		fprintf(stderr, "ERROR(simextinct): -r : 1 ~ %d\n", cafe_param->family_size.root_max);
		arraylist_free(pargs, free);
		return -1;
	}
	arraylist_free(pargs, free);

	int i, r;
	unsigned int accu_sum = 0;
	pHistogram phist_sim = histogram_new(NULL,0,0);
	pHistogram phist_accu = histogram_new(NULL,0,0);
	double* data = (double*) memory_new( num_trials, sizeof(double) );
	for ( r = range[0] ; r <= range[1] ; r++ )
	{
		int cnt_zero = 0;
		for(  i = 0 ; i < num_trials ; i++ )
		{
			cafe_tree_random_familysize(cafe_param->pcafe, r);
			data[i] = __cafe_cmd_extinct_count_zero((pTree)cafe_param->pcafe);
			cnt_zero += data[i];
		}
		cafe_log( cafe_param, "------------------------------------------\n");
		cafe_log( cafe_param, "Root size: %d\n", r );
		histogram_set_sparse_data(phist_sim,data,num_trials);
		histogram_merge( phist_accu, phist_sim );
		histogram_print(phist_sim,cafe_param->flog );
		if ( cafe_param->flog != stdout ) histogram_print(phist_sim,NULL);
		cafe_log( cafe_param, "Sum : %d\n", cnt_zero );
		accu_sum +=  cnt_zero;
	}

	cafe_log( cafe_param, "------------------------------------------\n");
	cafe_log( cafe_param, "Total\n", r );
	histogram_print(phist_accu,cafe_param->flog );
	if ( cafe_param->flog != stdout ) histogram_print(phist_accu,NULL);
	cafe_log( cafe_param, "Sum : %d\n", accu_sum );


	histogram_free(phist_sim);
	histogram_free(phist_accu);
	tree_clear_reg( (pTree)cafe_param->pcafe );
	memory_free(data);
	data = NULL;
	return 0;
}

double __hg_norm_cdf_func(double p, double* args)
{
	return normcdf(p, args[0], args[1]);
}

void __hg_print_sim_extinct(pHistogram** phist_sim_n, pHistogram* phist_sim,  
		                    int r, pHistogram phist_tmp, double* cnt, int num_trials)
{
	int j, t;
	double args[2];
	double alpha;
	for ( j = 0 ; j < phist_sim[r]->nbins ; j++ )
	{
		for ( t = 0 ; t < num_trials ; t++ )
		{
			cnt[t] = histogram_get_count(phist_sim_n[t][r], phist_sim[r]->point[j]);
		}
		histogram_set_by_unit( phist_tmp, cnt, num_trials, 1 );
		args[0] = mean(cnt,num_trials);
		args[1] = sqrt(variance(cnt,num_trials));
		cafe_log(cafe_param, "%g\t%d\t%4.3f\t%g\t%g\t%g ~ %g\n",
				phist_sim[r]->point[j], phist_sim[r]->count[j], ((double)phist_sim[r]->count[j])/phist_sim[r]->nsamples,
				args[0], args[1], phist_tmp->min, phist_tmp->max );		
	}
	memset(cnt, 0, sizeof(double)*num_trials);
	double sum = 0;
	for ( j = 0 ; j < phist_sim[r]->nbins ; j++ )
	{
		double p = phist_sim[r]->point[j];
		if ( p == 0 ) continue;
		for ( t = 0 ; t < num_trials ; t++ )
		{
			double a = p * histogram_get_count(phist_sim_n[t][r], p );
			cnt[t] += a;
			sum += a;
		}
	}
	histogram_set_by_unit( phist_tmp, cnt, num_trials, 1);
	if ( phist_tmp->nbins > 10 )
	{
		histogram_set_by_bin( phist_tmp, cnt, num_trials, 10 );
	}
	args[0] = mean(cnt,num_trials);
	args[1] = sqrt(variance(cnt,num_trials));
	alpha = histogram_check_fitness( phist_tmp, args, __hg_norm_cdf_func );
	cafe_log(cafe_param, "Extinct: %g\t%g\t%g\t%g\t%g ~ %g\n", 
			sum, args[0], args[1], alpha,
			args[0] - 1.96 * args[1], 
			args[0] + 1.96 * args[1]);
	histogram_print( phist_tmp, cafe_param->flog );
}

/**
\ingroup Commands
\brief Specify root family size distribution for simulation

Arguments: -i input file.
*/
int cafe_cmd_root_dist(int argc, char* argv[])
{
	pArrayList pargs = cafe_shell_build_argument(argc, argv);
	pArgument parg;
	STDERR_IF( cafe_param->pcafe == NULL, "ERROR(rootdist): You did not specify tree: command 'tree'\n" );
	
	if ( argc < 2 )
	{
		
		STDERR_IF( cafe_param->pfamily == NULL, "ERROR(rootdist): You did not load family: command 'load'\n" );
		STDERR_IF( cafe_param->lambda == NULL, "ERROR(rootdist): You did not set the parameters: command 'lambda' or 'lambdamu'\n" );
		cafe_log( cafe_param, "-----------------------------------------------------------\n");
		cafe_log( cafe_param, "Family information: %s\n", cafe_param->str_fdata->buf );
		cafe_log( cafe_param, "Log: %s\n", cafe_param->flog == stdout ? "stdout" : cafe_param->str_log->buf );
		if( cafe_param->pcafe ) 
		{
			pString pstr = phylogeny_string( (pTree)cafe_param->pcafe, NULL );
			cafe_log( cafe_param, "Tree: %s\n", pstr->buf ); 
			string_free(pstr);
		}
		if ( cafe_param->lambda )
		{
			pString pstr = cafe_tree_string_with_lambda(cafe_param->pcafe);
			cafe_log( cafe_param, "Lambda: %s\n", pstr->buf );
			string_free(pstr);
		}
		cafe_log( cafe_param, "The number of families is %d\n", cafe_param->pfamily->flist->size );
		int i;
		pCafeTree pcafe = cafe_param->pcafe;

		reset_birthdeath_cache(cafe_param->pcafe, cafe_param->parameterized_k_value, &cafe_param->family_size);
		for(i=0; i< cafe_param->pfamily->flist->size; i++) 
		{
			cafe_family_set_size(cafe_param->pfamily,i, pcafe);
			cafe_tree_viterbi(pcafe);
			cafe_log( cafe_param, "%d\n", ((pCafeNode)pcafe->super.root)->familysize);
		}
		cafe_free_birthdeath_cache(pcafe);
		cafe_log( cafe_param, "\n");
	}
	else if ((parg = cafe_shell_get_argument("-i", pargs)))
	{
		pString file = string_join(" ",parg->argc, parg->argv );
		FILE* fp = fopen(file->buf,"r");
		char buf[STRING_BUF_SIZE];
		if ( fp == NULL )
		{
			fprintf( stderr, "Cannot open file: %s\n", file->buf );
			return -1;
		}
		if ( fgets(buf,STRING_BUF_SIZE,fp) == NULL )
		{
			fclose(fp);
			fprintf( stderr, "Empty file: %s\n", file->buf );
			return -1;
		}
		int i=0;
		int max_rootsize = 0;
		string_pchar_chomp(buf);
		pArrayList data = string_pchar_split( buf, ' ');
		pArrayList max = string_pchar_split( data->array[data->size-1], ':');
		max_rootsize = atoi((char*)max->array[1]);
		arraylist_free(data,NULL);
		if (cafe_param->root_dist) { memory_free( cafe_param->root_dist); cafe_param->root_dist = NULL;}
		cafe_param->root_dist = (int*) memory_new(max_rootsize+1,sizeof(int));
		
		cafe_param->family_size.root_min = 1;
		cafe_param->family_size.root_max = max_rootsize;
		cafe_param->family_size.min = 0;
		cafe_param->family_size.max = max_rootsize * 2;
		copy_range_to_tree(cafe_param->pcafe, &cafe_param->family_size);
		
		for(  i = 0 ; fgets(buf,STRING_BUF_SIZE,fp) ; i++ )	
		{
			string_pchar_chomp(buf);
			data = string_pchar_split( buf, ' ');
			cafe_param->root_dist[atoi(data->array[0])] = atoi(data->array[1]);
		}
		arraylist_free(data,NULL);
	}
	arraylist_free(pargs, free);
	
	return 0;
}











void cafe_shell_free_errorstruct(pErrorStruct errormodel);


int cafe_shell_read_freq_from_measures(const char* file1, const char* file2, int* sizeFreq)
{
    int i=0;
    char buf1[STRING_BUF_SIZE];
    char buf2[STRING_BUF_SIZE];
    
    FILE* fpfile1 = fopen(file1,"r");
    if ( fpfile1 == NULL )
    {
        fprintf( stderr, "Cannot open file: %s\n", file1 );
        return -1;
    }
    if ( fgets(buf1,STRING_BUF_SIZE,fpfile1) == NULL )
    {
        fclose(fpfile1);
        fprintf( stderr, "Empty file: %s\n", file1 );
        return -1;
    }
    FILE* fpfile2 = NULL;
    if (file2) {
        fpfile2 = fopen(file2,"r");
        if ( fpfile2 == NULL )
        {
            fprintf( stderr, "Cannot open file: %s\n", file2 );
            return -1;
        }
        if ( fgets(buf2,STRING_BUF_SIZE,fpfile2) == NULL )
        {
            fclose(fpfile2);
            fprintf( stderr, "Empty file: %s\n", file2 );
            return -1;
        }
    }
    
    string_pchar_chomp(buf1);
    pArrayList data1 = string_pchar_split( buf1, '\t');
    arraylist_free(data1,NULL);        
    
    // count frequency of family sizes
    int line1 = 0;
    int maxFamilySize = cafe_param->family_size.max;
    int data1colnum = 0;
    while(fgets(buf1,STRING_BUF_SIZE,fpfile1))	
    {
        string_pchar_chomp(buf1);
        pArrayList data1 = string_pchar_split( buf1, '\t');
        for (i=2; i<data1->size; i++) {
            int size1 = atoi((char*)data1->array[i]);
            sizeFreq[size1]++;
            if (size1 > maxFamilySize) {
                maxFamilySize = size1;
            }
        }
        data1colnum = data1->size;
        arraylist_free(data1,NULL);
        line1++;
    }
    if (fpfile2) 
    {
        int line2 = 0;
        string_pchar_chomp(buf2);
        pArrayList data2 = string_pchar_split( buf2, '\t');
        if (data1colnum != data2->size) {
            fprintf(stderr,"file: the number of columns do not match between the two files\n");
            return -1;
        }
        arraylist_free(data2, NULL);

        while(fgets(buf2,STRING_BUF_SIZE,fpfile2))	
        {
            string_pchar_chomp(buf2);
            pArrayList data2 = string_pchar_split( buf2, '\t');
            for (i=2; i<data2->size; i++) {
                int size2 = atoi((char*)data2->array[i]);
                sizeFreq[size2]++;
                if (size2 > maxFamilySize) {
                    maxFamilySize = size2;
                }
            }
            arraylist_free(data2,NULL);
            line2++;
        }    
        if (line1 != line2) {
            fprintf(stderr,"ERROR: the number of lines do not match between the two files\n");
            return -1;
        }
    }
    return maxFamilySize;
}




int cafe_shell_read_error_double_measure(const char* error1, const char* error2, int** observed_pairs, int maxFamilySize)
{
	int i=0;
    int j=0;
    char buf1[STRING_BUF_SIZE];
    char buf2[STRING_BUF_SIZE];
    
    FILE* fperror1 = fopen(error1,"r");
    if ( fperror1 == NULL )
    {
        fprintf( stderr, "Cannot open file: %s\n", error1 );
        return -1;
    }
    if ( fgets(buf1,STRING_BUF_SIZE,fperror1) == NULL )
    {
        fclose(fperror1);
        fprintf( stderr, "Empty file: %s\n", error1 );
        return -1;
    }
    FILE* fperror2 = fopen(error2,"r");
    if ( fperror2 == NULL )
    {
        fprintf( stderr, "Cannot open file: %s\n", error2 );
        return -1;
    }
    if ( fgets(buf2,STRING_BUF_SIZE,fperror2) == NULL )
    {
        fclose(fperror2);
        fprintf( stderr, "Empty file: %s\n", error2 );
        return -1;
    }
    
  
    // now compare two files and count pairs.
    while(fgets(buf1,STRING_BUF_SIZE,fperror1))	
    {
        if ( fgets(buf2,STRING_BUF_SIZE,fperror2) != NULL ) {
            string_pchar_chomp(buf1);
            pArrayList data1 = string_pchar_split( buf1, '\t');
            string_pchar_chomp(buf2);
            pArrayList data2 = string_pchar_split( buf2, '\t');
            if (strcmp((char*)data1->array[1], (char*)data2->array[1])!= 0) {
                fprintf(stderr,"ERROR: the family IDs in each line do not match between the two files\n");      
                return -1;
            }
            // check pairs
            for (i=2; i<data1->size; i++) {
                int size1 = atoi((char*)data1->array[i]);
                int size2 = atoi((char*)data2->array[i]);
                observed_pairs[size1][size2]++;
            }
            arraylist_free(data1,NULL);
            arraylist_free(data2,NULL);
        }
    }
    
    // now make triangle matrix by merging i,j and j,i
    for (i=0; i<=maxFamilySize; i++) {
        for (j=0; j<i; j++) {
            observed_pairs[j][i] += observed_pairs[i][j];
            observed_pairs[i][j] = 0;
        }
    }
    
    
    return 0;
}

int cafe_shell_read_error_true_measure(const char* errorfile, const char* truefile, int** observed_pairs, int maxFamilySize)
{
	int i=0;
    char buf1[STRING_BUF_SIZE];
    char buf2[STRING_BUF_SIZE];
    
    FILE* fperror = fopen(errorfile,"r");
    if ( fperror == NULL )
    {
        fprintf( stderr, "Cannot open file: %s\n", errorfile );
        return -1;
    }
    if ( fgets(buf1,STRING_BUF_SIZE,fperror) == NULL )
    {
        fclose(fperror);
        fprintf( stderr, "Empty file: %s\n", errorfile );
        return -1;
    }
    FILE* fptruth = fopen(truefile,"r");
    if ( fptruth == NULL )
    {
        fprintf( stderr, "Cannot open file: %s\n", truefile );
        return -1;
    }
    if ( fgets(buf2,STRING_BUF_SIZE,fptruth) == NULL )
    {
        fclose(fptruth);
        fprintf( stderr, "Empty file: %s\n", truefile );
        return -1;
    }
    
    
    // now compare two files and count pairs.
    while(fgets(buf1,STRING_BUF_SIZE,fperror))	
    {
        if ( fgets(buf2,STRING_BUF_SIZE,fptruth) != NULL ) {
            string_pchar_chomp(buf1);
            pArrayList data1 = string_pchar_split( buf1, '\t');
            string_pchar_chomp(buf2);
            pArrayList data2 = string_pchar_split( buf2, '\t');
            if (strcmp((char*)data1->array[1], (char*)data2->array[1])!= 0) {
                fprintf(stderr,"ERROR: the family IDs in each line do not match between the two files\n");      
                return -1;
            }
            // check pairs
            for (i=2; i<data1->size; i++) {
                int size1 = atoi((char*)data1->array[i]);
                int size2 = atoi((char*)data2->array[i]);
                observed_pairs[size1][size2]++;
            }
            arraylist_free(data1,NULL);
            arraylist_free(data2,NULL);
        }
    }
    return 0;
}

// conditional probability of measuring i=familysize when true count is j
int __check_error_model_columnsums(pErrorStruct errormodel)
{
	int i, j = 0;
	int diff = errormodel->todiff;

	for (j = 0; j<diff; j++) {
		// column j
		double columnsum = 0;
		for (i = 0; i <= errormodel->maxfamilysize; i++) {
			columnsum += errormodel->errormatrix[i][j];
		}
		errormodel->errormatrix[0][j] = errormodel->errormatrix[0][j] + (1 - columnsum);
	}

	// all other columns
	for (j = diff; j <= errormodel->maxfamilysize - diff; j++) {
		double columnsum = 0;
		for (i = 0; i <= errormodel->maxfamilysize; i++) {
			columnsum += errormodel->errormatrix[i][j];
		}
		if (abs(1 - columnsum) > 0.00000000000001) {
			for (i = 0; i <= errormodel->maxfamilysize; i++) {
				errormodel->errormatrix[i][j] = errormodel->errormatrix[i][j] / columnsum;
			}
		}
	}

	for (j = errormodel->maxfamilysize - diff + 1; j <= errormodel->maxfamilysize; j++) {
		// column j
		double columnsum = 0;
		for (i = 0; i <= errormodel->maxfamilysize; i++) {
			columnsum += errormodel->errormatrix[i][j];
		}
		errormodel->errormatrix[errormodel->maxfamilysize][j] = errormodel->errormatrix[errormodel->maxfamilysize][j] + (1 - columnsum);

	}
	return 0;
}

pErrorStruct cafe_shell_create_error_matrix_from_estimate(pErrorMeasure errormeasure)
{
	int i = 0;
	int j = 0;

	// allocate new errormodel
	pErrorStruct errormodel = memory_new(1, sizeof(ErrorStruct));

	errormodel->maxfamilysize = errormeasure->maxFamilySize;
	errormodel->fromdiff = -(errormeasure->model_parameter_diff);
	errormodel->todiff = errormeasure->model_parameter_diff;
	errormodel->errorfilename = NULL;
	errormodel->errormatrix = (double**)memory_new_2dim(errormodel->maxfamilysize + 1, errormodel->maxfamilysize + 1, sizeof(double));

	int total_param_num = 0;
	double* total_params = NULL;
	if (errormeasure->b_symmetric) {
		// symmetric
		double sum = 0;
		total_param_num = errormeasure->model_parameter_number + errormeasure->model_parameter_diff + 1;
		total_params = memory_new(total_param_num, sizeof(double));
		total_params[errormeasure->model_parameter_diff] = errormeasure->estimates[0];
		sum = errormeasure->estimates[0];
		for (i = 1; i<errormeasure->model_parameter_number; i++) {
			total_params[errormeasure->model_parameter_diff + i] = errormeasure->estimates[i];
			sum += 2 * errormeasure->estimates[i];
		}
		total_params[total_param_num - 1] = (1 - sum) / (double)((errormeasure->maxFamilySize + 1) - (errormeasure->model_parameter_diff * 2 + 1));
		// now fill left side
		for (i = 0; i<errormeasure->model_parameter_diff; i++) {
			total_params[i] = total_params[abs(total_param_num - 1 - 1 - i)];
		}
	}
	else {
		//asymmetric
		double sum = 0;
		total_param_num = errormeasure->model_parameter_number + 1;
		total_params = memory_new(total_param_num, sizeof(double));
		for (i = 0; i<errormeasure->model_parameter_number; i++) {
			total_params[i] = errormeasure->estimates[i];
			sum += errormeasure->estimates[i];
		}
		total_params[total_param_num - 1] = (1 - sum) / (double)((errormeasure->maxFamilySize + 1) - (errormeasure->model_parameter_diff * 2 + 1));
	}


	// now fill the error matrix column by column
	for (j = 0; j <= errormodel->maxfamilysize; j++) {
		int k = 0;  // k is the index of total_params
		for (i = 0; i<errormodel->fromdiff + j; i++) {
			if (i <= errormodel->maxfamilysize) {
				errormodel->errormatrix[i][j] = total_params[total_param_num - 1]; // marginal error probability epsilon
			}
		}
		for (i = errormodel->fromdiff + j; i <= errormodel->todiff + j; i++) {
			if (i >= 0 && i <= errormodel->maxfamilysize) {
				errormodel->errormatrix[i][j] = total_params[k]; // conditional probability of measuring i+j when true count is j
			}
			k++;
		}
		for (i = errormodel->todiff + j + 1; i <= errormodel->maxfamilysize; i++) {
			if (i >= 0) {
				errormodel->errormatrix[i][j] = total_params[total_param_num - 1]; // marginal error probability epsilon
			}
		}
	}

	// now make sure that columns of the error matrix sums to one.
	__check_error_model_columnsums(errormodel);

	/*    if (cafe_param->pfamily->errors == NULL) {
	cafe_param->pfamily->errors = arraylist_new(cafe_param->pfamily->num_species);
	}
	arraylist_add(cafe_param->pfamily->errors, errormodel);*/
	return errormodel;
}

double __loglikelihood_pairs_from_double_measure(double* parameters, void* args)
{
	int i, j, k;
    
    pErrorMeasure errormeasure = (pErrorMeasure)args;
    double marginal_error_probability_epsilon = -1;   
    if (errormeasure->b_symmetric) { 
        // symmetric
        double sum = parameters[0];
        for(i=1; i<errormeasure->model_parameter_number; i++) {
            sum += 2*parameters[i];
        } 
        marginal_error_probability_epsilon = (1-sum)/(double)((errormeasure->maxFamilySize+1)-(errormeasure->model_parameter_diff*2+1));
    }   
    else {  
        //asymmetric
        double sum = 0;
        for(i=0; i<errormeasure->model_parameter_number; i++) {
            sum += parameters[i];
        } 
        marginal_error_probability_epsilon = (1-sum)/(double)((errormeasure->maxFamilySize+1)-(errormeasure->model_parameter_diff*2+1));
    }
    
    
	double score = 0;
	int skip = 0;
	for ( i = 0 ; i < errormeasure->model_parameter_number ; i++ )
	{
		if ( ( parameters[i] < 0 ) || ( marginal_error_probability_epsilon < 0 ) || (marginal_error_probability_epsilon > parameters[i]) )
		{ 
			skip  = 1;
			score = log(0);
			break;
		}
	}
	if ( !skip && errormeasure->b_peakzero ) {
        double previous_param = 0;
        if (errormeasure->b_symmetric) {
            previous_param = parameters[0]; 
            for (i = 1; i<errormeasure->model_parameter_number; i++) {
                if (previous_param < parameters[i]) {
                    skip  = 1;
                    score = log(0);
                    break;                
                }
                previous_param = parameters[i];
            }
        }
        else {
            previous_param = parameters[errormeasure->model_parameter_diff]; 
            for (i=1; i<=errormeasure->model_parameter_diff; i++) {
                if (previous_param < parameters[errormeasure->model_parameter_diff-i]) {
                    skip  = 1;
                    score = log(0);
                    break;                
                }
                previous_param = parameters[errormeasure->model_parameter_diff-i];
            }
            previous_param = parameters[errormeasure->model_parameter_diff]; 
            for (i=1; i<=errormeasure->model_parameter_diff; i++) {
                if (previous_param < parameters[errormeasure->model_parameter_diff+i]) {
                    skip  = 1;
                    score = log(0);
                    break;                
                }
                previous_param = parameters[errormeasure->model_parameter_diff+i];
            }
        }
    }
	if ( !skip )
	{
        errormeasure->estimates = parameters;
        pErrorStruct errormodel = cafe_shell_create_error_matrix_from_estimate(errormeasure);
        
        double** discord_prob_model = (double**)memory_new_2dim(errormeasure->maxFamilySize+1, errormeasure->maxFamilySize+1, sizeof(double));
        for (i=0; i<=errormeasure->maxFamilySize; i++) {
            for (j=i; j<=errormeasure->maxFamilySize; j++) {
                for (k=0; k<=errormeasure->maxFamilySize; k++) {
                    double pi_i_k = errormodel->errormatrix[i][k];
                    double pi_j_k = errormodel->errormatrix[j][k];
                    if (i==j) {
                        discord_prob_model[i][j] += errormeasure->sizeDist[k]*pi_i_k*pi_j_k;
                    }
                    else {
                        discord_prob_model[i][j] += 2*errormeasure->sizeDist[k]*pi_i_k*pi_j_k;                        
                    }
                }
            }
        }
        for (i=0; i<=errormeasure->maxFamilySize; i++) {
            for (j=i; j<=errormeasure->maxFamilySize; j++) {
                // add to the log likelihood
                double term = errormeasure->pairs[i][j]? errormeasure->pairs[i][j] * log(discord_prob_model[i][j]) : 0;
                score += term;
                if (isnan(score) || isinf(-score) || !isfinite(score)) {
                    cafe_log(cafe_param,"Score: %f\n", score);
                    break;
                }
            }
        }
        double prob00 = 0;
        for (k=0; k<=errormeasure->maxFamilySize; k++) {
            double pi_i_k = errormodel->errormatrix[0][k];
            double pi_j_k = errormodel->errormatrix[0][k];
            prob00 += errormeasure->sizeDist[k]*pi_i_k*pi_j_k;
        }
        score -= log(1-prob00);

        memory_free_2dim((void**)discord_prob_model, errormeasure->maxFamilySize+1, errormeasure->maxFamilySize+1, NULL);
        cafe_shell_free_errorstruct(errormodel);
        
    }
    
    char buf[STRING_STEP_SIZE];
	buf[0] = '\0';
	string_pchar_join_double(buf,",", errormeasure->model_parameter_number, parameters );
	cafe_log(cafe_param,"\tparameters : %s & Score: %f\n", buf, score);
    return -score;
}
 



double __loglikelihood_pairs_from_true_measure(double* parameters, void* args)
{
	int i, j;
    
    pErrorMeasure errormeasure = (pErrorMeasure)args;
    
    double marginal_error_probability_epsilon = 0;   
    if (errormeasure->b_symmetric) { 
        // symmetric
        double sum = parameters[0];
        for(i=1; i<errormeasure->model_parameter_number; i++) {
            sum += 2*parameters[i];
        } 
        marginal_error_probability_epsilon = (1-sum)/(double)((errormeasure->maxFamilySize+1)-(errormeasure->model_parameter_diff*2+1));
    }   
    else {  
        //asymmetric
        double sum = 0;
        for(i=0; i<errormeasure->model_parameter_number; i++) {
            sum += parameters[i];
        } 
        marginal_error_probability_epsilon = (1-sum)/(double)((errormeasure->maxFamilySize+1)-(errormeasure->model_parameter_diff*2+1));
    }
  
    
	double score = 0;
	int skip = 0;
	for ( i = 0 ; i < errormeasure->model_parameter_number ; i++ )
	{
		if ( ( parameters[i] < 0 ) || ( marginal_error_probability_epsilon < 0 ) || (marginal_error_probability_epsilon > parameters[i]) )
		{ 
			skip  = 1;
			score = log(0);
			break;
		}
	}
	if ( !skip && errormeasure->b_peakzero ) {
        double previous_param = 0;
        if (errormeasure->b_symmetric) {
            previous_param = parameters[0]; 
            for (i = 1; i<errormeasure->model_parameter_number; i++) {
                if (previous_param < parameters[i]) {
                    skip  = 1;
                    score = log(0);
                    break;                
                }
                previous_param = parameters[i];
            }
        }
        else {
            previous_param = parameters[errormeasure->model_parameter_diff]; 
            for (i=1; i<=errormeasure->model_parameter_diff; i++) {
                if (previous_param < parameters[errormeasure->model_parameter_diff-i]) {
                    skip  = 1;
                    score = log(0);
                    break;                
                }
                previous_param = parameters[errormeasure->model_parameter_diff-i];
            }
            previous_param = parameters[errormeasure->model_parameter_diff]; 
            for (i=1; i<=errormeasure->model_parameter_diff; i++) {
                if (previous_param < parameters[errormeasure->model_parameter_diff+i]) {
                    skip  = 1;
                    score = log(0);
                    break;                
                }
                previous_param = parameters[errormeasure->model_parameter_diff+i];
            }
        }
    }
	if ( !skip )
	{
        errormeasure->estimates = parameters;
        pErrorStruct errormodel = cafe_shell_create_error_matrix_from_estimate(errormeasure);
        
        double** discord_prob_model = (double**)memory_new_2dim(errormeasure->maxFamilySize+1, errormeasure->maxFamilySize+1, sizeof(double));
        for (i=0; i<=errormeasure->maxFamilySize; i++) {
            for (j=0; j<=errormeasure->maxFamilySize; j++) {
                // find discordance probability based on parameters
                double pi_i_j = errormodel->errormatrix[i][j];
                discord_prob_model[i][j] = errormeasure->sizeDist[j]*pi_i_j;
            }
        }
        for (i=0; i<=errormeasure->maxFamilySize; i++) {
            for (j=0; j<=errormeasure->maxFamilySize; j++) {
                // add to the log likelihood
                double term = errormeasure->pairs[i][j]? errormeasure->pairs[i][j] * log(discord_prob_model[i][j]) : 0;
                score += term;
                if (isnan(score) || isinf(-score)) {
                    cafe_log(cafe_param,"Score: %f\n", score);
                }
            }
        }
        double prob00 = errormodel->errormatrix[0][0]*errormeasure->sizeDist[0];
        score -= log(1-prob00);

        memory_free_2dim((void**)discord_prob_model, errormeasure->maxFamilySize+1, errormeasure->maxFamilySize+1, NULL);
        cafe_shell_free_errorstruct(errormodel);
        
    }
    
    char buf[STRING_STEP_SIZE];
	buf[0] = '\0';
	string_pchar_join_double(buf,",", errormeasure->model_parameter_number, parameters );
	cafe_log(cafe_param,"\tparameters : %s & Score: %f\n", buf, score);
    return -score;
}




pErrorMeasure cafe_shell_estimate_error_double_measure(const char* error1, const char* error2, int b_symmetric, int max_diff, int b_peakzero)
{
    int i;
    pCafeParam param = cafe_param;
    
    int* sizeFreq = memory_new(10000, sizeof(int)); 
    int maxFamilySize = cafe_shell_read_freq_from_measures(error1, error2, sizeFreq);
    if (maxFamilySize < 0 ) {
        fprintf(stderr,"ERROR: failed to read freqeuncy from measurement files\n");              
    }
    // get size probability distribution
    int sizeTotal = 0;
    for (i = 0; i<= maxFamilySize; i++) {
        sizeTotal += sizeFreq[i]+1;
        if (sizeTotal < 0) {
            fprintf(stderr,"ERROR: total freqeuncy is less than zero\n");                          
        }
    }
    double* sizeDist = (double*)memory_new(maxFamilySize+1, sizeof(double));
    for (i = 0; i<=maxFamilySize; i++) {
        sizeDist[i] = (sizeFreq[i]+1)/(double)sizeTotal;
        if (sizeDist[i] < 0) {
            fprintf(stderr,"ERROR: freqeuncy is less than zero\n");                          
        }
    }
    
    
    int** observed_pairs = (int**)memory_new_2dim(maxFamilySize+1, maxFamilySize+1, sizeof(int)); // need space for zero
    int retval = cafe_shell_read_error_double_measure(error1, error2, observed_pairs, maxFamilySize);
    if (retval < 0) {
        fprintf(stderr,"ERROR: failed to count pairs from measurement files\n");      
    }
    
    // set up parameters for ML
    pErrorMeasure error = memory_new(1, sizeof(ErrorMeasure));
    error->sizeDist = sizeDist;
    error->maxFamilySize = maxFamilySize;
    error->pairs = observed_pairs;
    error->b_symmetric = b_symmetric;
    error->b_peakzero = b_peakzero;
    if (b_symmetric) {
        // symmetric model (diff == number)
        error->model_parameter_diff = max_diff;
        error->model_parameter_number = max_diff+1;  
    }
    else {
        // asymmetric model (diff*2 == number)
        error->model_parameter_diff = max_diff;
        error->model_parameter_number = 2*max_diff+1;
    }
    
        
    // now estimate the misclassification rate 
    int max_runs = 100;
    int converged = 0;
	int runs = 0;
    double minscore = DBL_MAX;
    double* parameters = memory_new(error->model_parameter_number, sizeof(double));
	double* bestrun_parameters = memory_new(error->model_parameter_number, sizeof(double));
    
    do {
        pFMinSearch pfm;
        double* sorted_params = memory_new_with_init(error->model_parameter_number, sizeof(double), parameters);
        for (i=0; i<error->model_parameter_number; i++) {
            sorted_params[i] = unifrnd()/(double)error->model_parameter_number;
        }
        qsort (sorted_params, error->model_parameter_number, sizeof(double), comp_double);
        if (error->b_symmetric) {
            int j=0;
            for (i=error->model_parameter_number-1; i>=0; i--) {
                parameters[j++] = sorted_params[i];
            }
        }
        else {
            int j=error->model_parameter_number-1;
            parameters[error->model_parameter_diff] = sorted_params[j--];
            for (i=1; i<=error->model_parameter_diff; i++) {
                parameters[error->model_parameter_diff-i] = sorted_params[j--];
                parameters[error->model_parameter_diff+i] = sorted_params[j--];
            }
        }
        pfm = fminsearch_new_with_eq(__loglikelihood_pairs_from_double_measure, error->model_parameter_number, error);
        pfm->tolx = 1e-9;
        pfm->tolf = 1e-9;
        fminsearch_min(pfm, parameters);
        double *re = fminsearch_get_minX(pfm);
        for ( i = 0 ; i < error->model_parameter_number; i++ ) parameters[i] = re[i];
        cafe_log(param, "\n");
        cafe_log(param,"Misclassification Matrix Search Result: %d\n", pfm->iters );
        cafe_log(param, "Score: %f\n", *pfm->fv);
        
        if (runs > 0) {
			if (!isnan(*pfm->fv) && !isinf(*pfm->fv) && abs(minscore - (*pfm->fv)) < pfm->tolf) {
				converged = 1;
			}
		}
        if (pfm->iters < pfm->maxiters) {
            if ( *pfm->fv < minscore) {
                minscore = *pfm->fv;
                memcpy(bestrun_parameters, parameters, (error->model_parameter_number)*sizeof(double));
            }
            runs++;
        }
/*        else {
            cafe_log(param,"what went wrong?\n");
            fminsearch_min(pfm, parameters);
        }*/
		fminsearch_free(pfm);
	} while (!converged && runs<max_runs); 

		if (converged) {
			cafe_log(param,"score converged in %d runs.\n", runs);
		}
		else {
			cafe_log(param,"score failed to converge in %d runs.\n", max_runs);
			cafe_log(param,"best score: %f\n", minscore);            
		}
	memory_free(parameters);      
    error->estimates = bestrun_parameters;
    
    //memory_free(error);           // we are going to return these values
    memory_free_2dim((void**)observed_pairs, maxFamilySize+1, maxFamilySize+1, NULL);
    memory_free(sizeFreq);
    return error; 
}



pErrorMeasure cafe_shell_estimate_error_true_measure(const char* errorfile, const char* truefile, int b_symmetric, int max_diff, int b_peakzero)
{
    int i;
    pCafeParam param = cafe_param;
    
    int* sizeFreq = memory_new(10000, sizeof(int)); 
    int maxFamilySize = cafe_shell_read_freq_from_measures(truefile, errorfile, sizeFreq);
    if (maxFamilySize < 0 ) {
        fprintf(stderr,"ERROR: failed to read freqeuncy from measurement files\n");              
    }
    // get size probability distribution
    int sizeTotal = 0;
    for (i = 0; i<= maxFamilySize; i++) {
        sizeTotal += sizeFreq[i]+1;
    }
    double* sizeDist = (double*)memory_new(maxFamilySize+1, sizeof(double));
    for (i = 0; i<=maxFamilySize; i++) {
        sizeDist[i] = (sizeFreq[i]+1)/(double)sizeTotal;
    }
    
    
    int** observed_pairs = (int**)memory_new_2dim(maxFamilySize+1, maxFamilySize+1, sizeof(int)); // need space for zero
    int retval = cafe_shell_read_error_true_measure(errorfile, truefile, observed_pairs, maxFamilySize);
    if (retval < 0) {
        fprintf(stderr,"ERROR: failed to count pairs from measurement files\n");      
    }
    
    // set up parameters for ML
    pErrorMeasure error = memory_new(1, sizeof(ErrorMeasure));
    error->sizeDist = sizeDist;
    error->maxFamilySize = maxFamilySize;
    error->pairs = observed_pairs;
    error->b_symmetric = b_symmetric;
    error->b_peakzero = b_peakzero;
    if (b_symmetric) {
        // symmetric model (diff == number)
        error->model_parameter_diff = max_diff;
        error->model_parameter_number = max_diff+1;  
    }
    else {
        // asymmetric model (diff*2 == number)
        error->model_parameter_diff = max_diff;
        error->model_parameter_number = 2*max_diff+1;
    }
    
    // now estimate the misclassification rate 
    int max_runs = 100;
    int converged = 0;
	int runs = 0;
    double minscore = DBL_MAX;
    double* parameters = memory_new(error->model_parameter_number, sizeof(double));
	double* bestrun_parameters = memory_new(error->model_parameter_number, sizeof(double));
    
    do {
        pFMinSearch pfm;
        double* sorted_params = memory_new_with_init(error->model_parameter_number, sizeof(double), parameters);
        for (i=0; i<error->model_parameter_number; i++) {
            sorted_params[i] = unifrnd()/(double)error->model_parameter_number;
        }
        qsort (sorted_params, error->model_parameter_number, sizeof(double), comp_double);
        if (error->b_symmetric) {
            int j=0;
            for (i=error->model_parameter_number-1; i>=0; i--) {
                parameters[j++] = sorted_params[i];
            }
        }
        else {
            int j=error->model_parameter_number-1;
            parameters[error->model_parameter_diff] = sorted_params[j--];
            for (i=1; i<=error->model_parameter_diff; i++) {
                parameters[error->model_parameter_diff-i] = sorted_params[j--];
                parameters[error->model_parameter_diff+i] = sorted_params[j--];
            }
        }
        
        pfm = fminsearch_new_with_eq(__loglikelihood_pairs_from_true_measure, error->model_parameter_number, error);
        pfm->tolx = 1e-9;
        pfm->tolf = 1e-9;
        fminsearch_min(pfm, parameters);
        double *re = fminsearch_get_minX(pfm);
        for ( i = 0 ; i < error->model_parameter_number; i++ ) parameters[i] = re[i];
        cafe_log(param, "\n");
        cafe_log(param,"Misclassification Matrix Search Result: %d\n", pfm->iters );
        cafe_log(param, "Score: %f\n", *pfm->fv);
        
        if (runs > 0) {
			if (!isnan(*pfm->fv) && !isinf(*pfm->fv) && abs(minscore - (*pfm->fv)) < pfm->tolf) {
				converged = 1;
			}
		}
        if (pfm->iters < pfm->maxiters) {
            if ( *pfm->fv < minscore) {
                minscore = *pfm->fv;
                memcpy(bestrun_parameters, parameters, (error->model_parameter_number)*sizeof(double));
            }
            runs++;
        }
		fminsearch_free(pfm);
	} while (!converged && runs<max_runs); 
    
    if (converged) {
        cafe_log(param,"score converged in %d runs.\n", runs);
    }
    else {
        cafe_log(param,"score failed to converge in %d runs.\n", max_runs);
        cafe_log(param,"best score: %f\n", minscore);            
    }
	memory_free(parameters);      
    error->estimates = bestrun_parameters;
    
    //memory_free(error);           // we are going to return these values
    memory_free_2dim((void**)observed_pairs, maxFamilySize+1, maxFamilySize+1, NULL);
    memory_free(sizeFreq);
    return error; 
}

void cafe_shell_free_errorstruct(pErrorStruct errormodel)
{
    if (errormodel->errorfilename) {
        memory_free(errormodel->errorfilename);
        errormodel->errorfilename = NULL;
    }
    if (errormodel->errormatrix) {
        memory_free_2dim((void**)errormodel->errormatrix, errormodel->maxfamilysize+1, errormodel->maxfamilysize+1, NULL);  
        errormodel->errormatrix = NULL;
    }
}

