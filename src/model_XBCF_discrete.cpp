#include "tree.h"
#include "model.h"
#include <cfenv>

//////////////////////////////////////////////////////////////////////////////////////
//
//
//  Normal Linear Model for continuous treatment XBCF
//
//
//////////////////////////////////////////////////////////////////////////////////////

void XBCFDiscreteModel::incSuffStat(State &state, size_t index_next_obs, std::vector<double> &suffstats)
{
    if (state.treatment_flag)
    {
        // treatment forest
        if ((*state.Z_std)[0][index_next_obs] == 1)
        {
            // if treated
            suffstats[1] += ((*state.y_std)[index_next_obs] - state.a * (*state.mu_fit)[index_next_obs] - state.b_vec[1] * (*state.tau_fit)[index_next_obs]) / state.b_vec[1];
            suffstats[3] += 1;
        }
        else
        {
            // if control group
            suffstats[0] += ((*state.y_std)[index_next_obs] - state.a * (*state.mu_fit)[index_next_obs] - state.b_vec[0] * (*state.tau_fit)[index_next_obs]) / state.b_vec[0];
            suffstats[2] += 1;
        }
    }
    else
    {
        // prognostic forest
        if ((*state.Z_std)[0][index_next_obs] == 1)
        {
            // if treated
            suffstats[1] += ((*state.y_std)[index_next_obs] - state.a * (*state.mu_fit)[index_next_obs] - state.b_vec[1] * (*state.tau_fit)[index_next_obs]) / state.a;
            suffstats[3] += 1;
        }
        else
        {
            // if control group
            suffstats[0] += ((*state.y_std)[index_next_obs] - state.a * (*state.mu_fit)[index_next_obs] - state.b_vec[0] * (*state.tau_fit)[index_next_obs]) / state.a;
            suffstats[2] += 1;
        }
    }
    return;
}

void XBCFDiscreteModel::samplePars(State &state, std::vector<double> &suff_stat, std::vector<double> &theta_vector, double &prob_leaf)
{
    std::normal_distribution<double> normal_samp(0.0, 1.0);

    double tau_use;

    if (state.treatment_flag)
    {
        tau_use = tau_mod;
    }
    else
    {
        tau_use = tau_con;
    }

    double sigma2 = pow(state.sigma, 2);

    theta_vector[0] = suff_stat[1] / sigma2 / (suff_stat[0] / sigma2 + 1.0 / tau_use) + sqrt(1.0 / (1.0 / tau_use + suff_stat[0] / sigma2)) * normal_samp(state.gen);

    return;
}

void XBCFDiscreteModel::update_state(State &state, size_t tree_ind, X_struct &x_struct)
{
    // Draw Sigma
    std::vector<double> full_residual_trt(state.N_trt);
    std::vector<double> full_residual_ctrl(state.N_ctrl);

    // index
    size_t index_trt = 0;
    size_t index_ctrl = 0;

    for (size_t i = 0; i < state.n_y; i++)
    {
        if ((*state.Z_std)[0][i] == 1)
        {
            // if treated
            full_residual_trt[index_trt] = (*state.y_std)[i] - state.a * (*state.mu_fit)[i] - state.b_vec[1] * (*state.tau_fit)[i];
            index_trt++;
        }
        else
        {
            // if control group
            full_residual_ctrl[index_ctrl] = (*state.y_std)[i] - state.a * (*state.mu_fit)[i] - state.b_vec[0] * (*state.tau_fit)[i];
            index_ctrl++;
        }
    }
    
    std::gamma_distribution<double> gamma_samp1((state.N_trt + kap) / 2.0, 2.0 / (sum_squared(full_residual_trt) + s));

    std::gamma_distribution<double> gamma_samp0((state.N_ctrl + kap) / 2.0, 2.0 / (sum_squared(full_residual_ctrl) + s));

    if (state.treatment_flag)
    {
        state.update_sigma(1.0 / sqrt(gamma_samp1(state.gen)));
    }
    else
    {
        state.update_sigma(1.0 / sqrt(gamma_samp0(state.gen)));
    }
    return;
}

void XBCFDiscreteModel::update_tau(State &state, size_t tree_ind, size_t sweeps, vector<vector<tree>> &trees)
{
    std::vector<tree *> leaf_nodes;
    trees[sweeps][tree_ind].getbots(leaf_nodes);
    double sum_squared = 0.0;
    for (size_t i = 0; i < leaf_nodes.size(); i++)
    {
        sum_squared = sum_squared + pow(leaf_nodes[i]->theta_vector[0], 2);
    }

    double kap = (state.treatment_flag) ? this->tau_mod_kap : this->tau_con_kap;

    double s = (state.treatment_flag) ? this->tau_mod_s * this->tau_mod_mean : this->tau_con_s * this->tau_con_mean;

    std::gamma_distribution<double> gamma_samp((leaf_nodes.size() + kap) / 2.0, 2.0 / (sum_squared + s));

    double tau_sample = 1.0 / gamma_samp(state.gen);

    if (state.treatment_flag)
    {
        this->tau_mod = tau_sample;
    }
    else
    {
        this->tau_con = tau_sample;
    }

    return;
};

void XBCFDiscreteModel::update_tau_per_forest(State &state, size_t sweeps, vector<vector<tree>> &trees)
{
    std::vector<tree *> leaf_nodes;
    for (size_t tree_ind = 0; tree_ind < state.num_trees; tree_ind++)
    {
        trees[sweeps][tree_ind].getbots(leaf_nodes);
    }
    double sum_squared = 0.0;
    for (size_t i = 0; i < leaf_nodes.size(); i++)
    {
        sum_squared = sum_squared + pow(leaf_nodes[i]->theta_vector[0], 2);
    };

    double kap = (state.treatment_flag) ? this->tau_mod_kap : this->tau_con_kap;

    double s = (state.treatment_flag) ? this->tau_mod_s * this->tau_mod_mean : this->tau_con_s * this->tau_con_mean;

    std::gamma_distribution<double> gamma_samp((leaf_nodes.size() + kap) / 2.0, 2.0 / (sum_squared + s));
    double tau_sample = 1.0 / gamma_samp(state.gen);

    if (state.treatment_flag)
    {
        this->tau_mod = tau_sample;
    }
    else
    {
        this->tau_con = tau_sample;
    }
    return;
}

void XBCFDiscreteModel::initialize_root_suffstat(State &state, std::vector<double> &suff_stat)
{
    suff_stat.resize(4);
    std::fill(suff_stat.begin(), suff_stat.end(), 0.0);
    for (size_t i = 0; i < state.n_y; i++)
    {
        incSuffStat(state, i, suff_stat);
    }
    return;
}

void XBCFDiscreteModel::updateNodeSuffStat(State &state, std::vector<double> &suff_stat, matrix<size_t> &Xorder_std, size_t &split_var, size_t row_ind)
{
    if (state.treatment_flag)
    {
        // sum of z^2
        suff_stat[0] += pow(((*state.Z_std))[0][Xorder_std[split_var][row_ind]], 2);

        // sum of partial residual * z^2 (in y scale)
        suff_stat[1] += ((*state.residual_std)[0])[Xorder_std[split_var][row_ind]] * pow(((*state.Z_std))[0][Xorder_std[split_var][row_ind]], 2);

        // number of data points
        suff_stat[2] += 1;
    }
    else
    {
        suff_stat[0] += 1;
        suff_stat[1] += ((*state.residual_std)[0])[Xorder_std[split_var][row_ind]];
        suff_stat[2] += 1;
    }

    return;
}

void XBCFDiscreteModel::calculateOtherSideSuffStat(std::vector<double> &parent_suff_stat, std::vector<double> &lchild_suff_stat, std::vector<double> &rchild_suff_stat, size_t &N_parent, size_t &N_left, size_t &N_right, bool &compute_left_side)
{
    // in function split_xorder_std_categorical, for efficiency, the function only calculates suff stat of ONE child
    // this function calculate the other side based on parent and the other child
    if (compute_left_side)
    {
        rchild_suff_stat = parent_suff_stat - lchild_suff_stat;
    }
    else
    {
        lchild_suff_stat = parent_suff_stat - rchild_suff_stat;
    }
    return;
}

// void XBCFDiscreteModel::state_sweep(State&state, size_t tree_ind, size_t M, X_struct &x_struct) const
// {
//     size_t next_index = tree_ind + 1;
//     if (next_index == M)
//     {
//         next_index = 0;
//     }

//     for (size_t i = 0; i < (*state.residual_std)[0].size(); i++)
//     {
//         (*state.residual_std)[0][i] = (*state.residual_std)[0][i] - (*(x_struct.data_pointers[tree_ind][i]))[0] + (*(x_struct.data_pointers[next_index][i]))[0];
//     }
//     return;
// }

double XBCFDiscreteModel::likelihood(std::vector<double> &temp_suff_stat, std::vector<double> &suff_stat_all, size_t N_left, bool left_side, bool no_split, State &state) const
{
    // likelihood equation,

    double sigma2 = state.sigma2;

    size_t nb;
    double s0; // sum z_i^2
    double s1; // sum r_i * z_i^2

    if (no_split)
    {
        // calculate likelihood for no-split option (early stop)
        s0 = suff_stat_all[0];
        s1 = suff_stat_all[1];
        nb = suff_stat_all[2];
    }
    else
    {
        // calculate likelihood for regular split point
        if (left_side)
        {
            s0 = temp_suff_stat[0];
            s1 = temp_suff_stat[1];
            nb = N_left + 1;
        }
        else
        {
            s0 = suff_stat_all[0] - temp_suff_stat[0];
            s1 = suff_stat_all[1] - temp_suff_stat[1];
            nb = suff_stat_all[2] - N_left - 1;
        }
    }

    double tau_use;

    if (state.treatment_flag)
    {
        tau_use = tau_mod;
    }
    else
    {
        tau_use = tau_con;
    }

    return 0.5 * log(1.0 / (1.0 + tau_use * s0 / sigma2)) + 0.5 * pow(s1 / sigma2, 2) / (s0 / sigma2 + 1.0 / tau_use);
}

void XBCFDiscreteModel::ini_residual_std(State &state)
{
    // initialize the vector of full residuals
    for (size_t i = 0; i < (*state.residual_std)[0].size(); i++)
    {
        (*state.residual_std)[0][i] = (*state.y_std)[i] - (*state.mu_fit)[i] - ((*state.Z_std)[0][i]) * (*state.tau_fit)[i];
    }
}

void XBCFDiscreteModel::predict_std(matrix<double> &Ztestpointer, const double *Xtestpointer_con, const double *Xtestpointer_mod, size_t N_test, size_t p_con, size_t p_mod, size_t num_trees_con, size_t num_trees_mod, size_t num_sweeps, matrix<double> &yhats_test_xinfo, matrix<double> &prognostic_xinfo, matrix<double> &treatment_xinfo, vector<vector<tree>> &trees_con, vector<vector<tree>> &trees_mod)
{
    // predict the output as a matrix
    matrix<double> output_mod;

    // row : dimension of theta, column : number of trees
    ini_matrix(output_mod, this->dim_theta, trees_mod[0].size());

    matrix<double> output_con;
    ini_matrix(output_con, this->dim_theta, trees_con[0].size());

    for (size_t sweeps = 0; sweeps < num_sweeps; sweeps++)
    {
        for (size_t data_ind = 0; data_ind < N_test; data_ind++)
        {
            getThetaForObs_Outsample(output_mod, trees_mod[sweeps], data_ind, Xtestpointer_mod, N_test, p_mod);

            getThetaForObs_Outsample(output_con, trees_con[sweeps], data_ind, Xtestpointer_con, N_test, p_con);

            // take sum of predictions of each tree, as final prediction
            for (size_t i = 0; i < trees_mod[0].size(); i++)
            {
                treatment_xinfo[sweeps][data_ind] += output_mod[i][0];
            }

            for (size_t i = 0; i < trees_con[0].size(); i++)
            {
                prognostic_xinfo[sweeps][data_ind] += output_con[i][0];
            }

            yhats_test_xinfo[sweeps][data_ind] = prognostic_xinfo[sweeps][data_ind] + (Ztestpointer[0][data_ind]) * treatment_xinfo[sweeps][data_ind];
        }
    }
    return;
}

void XBCFDiscreteModel::ini_tau_mu_fit(State &state)
{
    double value = state.ini_var_yhat;
    for (size_t i = 0; i < (*state.residual_std)[0].size(); i++)
    {
        (*state.mu_fit)[i] = 0;
        (*state.tau_fit)[i] = value;
    }
    return;
}

void XBCFDiscreteModel::set_treatmentflag(State &state, bool value)
{
    state.treatment_flag = value;
    if (value)
    {
        // if treatment forest
        state.p = state.p_mod;
        state.p_categorical = state.p_categorical_mod;
        state.p_continuous = state.p_continuous_mod;
        state.Xorder_std = state.Xorder_std_mod;
        state.mtry = state.mtry_mod;
        state.num_trees = state.num_trees_mod;
        this->alpha = this->alpha_mod;
        this->beta = this->beta_mod;
    }
    else
    {
        state.p = state.p_con;
        state.p_categorical = state.p_categorical_con;
        state.p_continuous = state.p_continuous_con;
        state.Xorder_std = state.Xorder_std_con;
        state.mtry = state.mtry_con;
        state.num_trees = state.num_trees_con;
        this->alpha = this->alpha_con;
        this->beta = this->beta_con;
    }
    return;
}

void XBCFDiscreteModel::subtract_old_tree_fit(size_t tree_ind, State &state, X_struct &x_struct)
{
    if (state.treatment_flag)
    {
        for (size_t i = 0; i < (*state.tau_fit).size(); i++)
        {
            (*state.tau_fit)[i] -= (*(x_struct.data_pointers[tree_ind][i]))[0];
        }
    }
    else
    {
        for (size_t i = 0; i < (*state.mu_fit).size(); i++)
        {
            (*state.mu_fit)[i] -= (*(x_struct.data_pointers[tree_ind][i]))[0];
        }
    }
    return;
}

void XBCFDiscreteModel::add_new_tree_fit(size_t tree_ind, State &state, X_struct &x_struct)
{

    if (state.treatment_flag)
    {
        for (size_t i = 0; i < (*state.tau_fit).size(); i++)
        {
            (*state.tau_fit)[i] += (*(x_struct.data_pointers[tree_ind][i]))[0];
        }
    }
    else
    {
        for (size_t i = 0; i < (*state.mu_fit).size(); i++)
        {
            (*state.mu_fit)[i] += (*(x_struct.data_pointers[tree_ind][i]))[0];
        }
    }
    return;
}

void XBCFDiscreteModel::update_partial_residuals(size_t tree_ind, State &state, X_struct &x_struct)
{
    if (state.treatment_flag)
    {
        // treatment forest
        // (y - mu - Z * tau) / Z
        for (size_t i = 0; i < (*state.tau_fit).size(); i++)
        {
            ((*state.residual_std))[0][i] = ((*state.y_std)[i] - (*state.mu_fit)[i] - ((*state.Z_std)[0][i]) * (*state.tau_fit)[i]) / ((*state.Z_std)[0][i]);
        }
    }
    else
    {
        // prognostic forest
        // (y - mu - Z * tau)
        for (size_t i = 0; i < (*state.tau_fit).size(); i++)
        {
            ((*state.residual_std))[0][i] = ((*state.y_std)[i] - (*state.mu_fit)[i] - ((*state.Z_std)[0][i]) * (*state.tau_fit)[i]);
        }
    }
    return;
}

void XBCFDiscreteModel::update_split_counts(State &state, size_t tree_ind)
{
    if (state.treatment_flag)
    {
        (*state.mtry_weight_current_tree_mod) = (*state.mtry_weight_current_tree_mod) + (*state.split_count_current_tree);
        (*state.split_count_all_tree_mod)[tree_ind] = (*state.split_count_current_tree);
    }
    else
    {
        (*state.mtry_weight_current_tree_con) = (*state.mtry_weight_current_tree_con) + (*state.split_count_current_tree);
        (*state.split_count_all_tree_con)[tree_ind] = (*state.split_count_current_tree);
    }
    return;
}

void XBCFDiscreteModel::update_a(State &state)
{
    // update parameter a, y = a * mu + b_z * tau

    std::normal_distribution<double> normal_samp(0.0, 1.0);

    double mu2sum_ctrl = 0;
    double mu2sum_trt = 0;
    double muressum_ctrl = 0;
    double muressum_trt = 0;

    // compute the residual y - b * tau(x)

    for (size_t i = 0; i < state.n_y; i++)
    {
        if ((*state.Z_std)[0][i] == 1)
        {
            // if treated
            state.residual[i] = (*state.y_std)[i] - (*state.tau_fit)[i] * state.b_vec[1];
        }
        else
        {
            state.residual[i] = (*state.y_std)[i] - (*state.tau_fit)[i] * state.b_vec[0];
        }
    }
    for (size_t i = 0; i < state.n_y; i++)
    {
        if ((*state.Z_std)[0][i] == 1)
        {
            // if treated
            mu2sum_trt += pow((*state.mu_fit)[i], 2);
            muressum_trt += (*state.mu_fit)[i] * state.residual[i];
        }
        else
        {
            mu2sum_ctrl += pow((*state.mu_fit)[i], 2);
            muressum_ctrl += (*state.mu_fit)[i] * state.residual[i];
        }
    }
    // update parameters
    double v0 = 1.0 / (1.0 + mu2sum_ctrl / pow(state.sigma_vec[0], 2));
    double m0 = v0 * (muressum_ctrl) / pow(state.sigma_vec[0], 2);
    double v1 = 1 / (1.0 / v0 + mu2sum_trt / pow(state.sigma_vec[1], 2));
    double m1 = v1 * (m0 / v0 + (muressum_trt) / pow(state.sigma_vec[1], 2));

    state.a = m1 + sqrt(v1) * normal_samp(state.gen);

    return;
}

void XBCFDiscreteModel::update_b(State &state)
{
    // update b0 and b1 for XBCF discrete treatment

    std::normal_distribution<double> normal_samp(0.0, 1.0);

    double tau2sum_ctrl = 0;
    double tau2sum_trt = 0;
    double tauressum_ctrl = 0;
    double tauressum_trt = 0;

    // compute the residual y-a*mu(x) using state's objects y_std, mu_fit and a
    for (size_t i = 0; i < state.n_y; i++)
    {
        state.residual[i] = (*state.y_std)[i] - state.a * (*state.mu_fit)[i];
    }

    for (size_t i = 0; i < state.n_y; i++)
    {
        if ((*state.Z_std)[0][i] == 1)
        {
            tau2sum_trt += pow((*state.tau_fit)[i], 2);
            tauressum_trt += (*state.tau_fit)[i] * state.residual[i];
        }
        else
        {
            tau2sum_ctrl += pow((*state.tau_fit)[i], 2);
            tauressum_ctrl += (*state.tau_fit)[i] * state.residual[i];
        }
    }

    // update parameters
    double v0 = 1.0 / (2.0 + tau2sum_ctrl / pow(state.sigma_vec[0], 2));
    double v1 = 1.0 / (2.0 + tau2sum_trt / pow(state.sigma_vec[1], 2));

    double m0 = v0 * (tauressum_ctrl) / pow(state.sigma_vec[0], 2);
    double m1 = v1 * (tauressum_trt) / pow(state.sigma_vec[1], 2);

    // sample b0, b1
    double b0 = m0 + sqrt(v0) * normal_samp(state.gen);
    double b1 = m1 + sqrt(v1) * normal_samp(state.gen);

    state.b_vec[1] = b1;
    state.b_vec[0] = b0;

    return;
}