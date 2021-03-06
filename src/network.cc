#include "network.h"

#include <cstring>
#include <sstream>

#include <iostream>
#include <fstream>

Network::Network(World *world, Monitor *monitor) {
  m_mean_field = true;
  m_world = world;
  m_monitor = monitor;
  monitor->set_network(this);

  m_rng = gsl_rng_alloc(gsl_rng_taus);

  m_num_layers = 4;
  m_layer_sizes = new int[m_num_layers];
  m_layer_sizes[0] = 784;
  m_layer_sizes[1] = 500;
  m_layer_sizes[2] = 510; // Special case
  m_layer_sizes[3] = 2000;

  m_layers = new Layer*[m_num_layers];
  m_connections = new Connection*[m_num_layers - 1];

  for(int i = 0; i < m_num_layers; ++i) {
    bool labels = (i == 2);
    m_layers[i] = new Layer(m_layer_sizes[i], labels);
  }

  for(int i = 0; i < m_num_layers - 1; ++i) {
    m_connections[i] = new Connection(m_layers[i], m_layers[i + 1]);
  }
}

Network::~Network() {
  gsl_rng_free(m_rng);

  delete[] m_layer_sizes;

  for(int i = 0; i < m_num_layers; ++i) {
    delete m_layers[i];
  }
  delete[] m_layers;
    
  for(int i = 0; i < m_num_layers - 1; ++i) {
    delete m_connections[i];
  }
  delete[] m_connections;
}

void Network::run_step(Schedule *schedule) {
  if(schedule->step_type() == 0) { // Training step
    greedily_train_layer(m_rng, m_world->training_data(), schedule->target_layer(), schedule);
  }
  else if(schedule->step_type() == 1) { // Test classification of a single example
    gsl_vector *input_observations = gsl_vector_alloc(784); // TODO: Should be okay for dataset to own this rather than copying
    m_world->training_data()->get_state(input_observations, schedule->active_image());
    classify(input_observations);
    gsl_vector_free(input_observations);
  }
  else if(schedule->step_type() == 2) { // Post training fine tuning
    gsl_vector *input_observations = gsl_vector_alloc(784); // TODO: Should be okay for dataset to own this rather than copying
    m_world->training_data()->get_state(input_observations, schedule->active_image());
    fine_tune(input_observations, schedule->active_image());
    gsl_vector_free(input_observations);
  }
  else if(schedule->step_type() == 3) { // Sampling input units with fixed labels
    sample_input(m_rng, schedule->active_label());
  }
}

void Network::train(gsl_rng *rng, Dataset *training_data, Schedule *schedule) {
  m_monitor->log_event("Starting network training");
  schedule->reset();
    while(schedule->step()) {
    //    std::cout << "layer " << schedule->target_layer() << std::endl;
    greedily_train_layer(rng, training_data, schedule->target_layer(), schedule);
  }
}

void Network::sample_input(gsl_rng *rng, int label) {
  m_layers[m_num_layers - 1]->activate_from_bias();
  m_connections[m_num_layers - 2]->sample_layer(rng, 1000, label);
  for(int i = m_num_layers - 3; i >= 0; --i) {
    m_connections[i]->propagate_hidden(rng);
  }

  // Something something display output
}

int Network::classify(gsl_vector *observations) {
  m_layers[0]->set_state(observations);
  for(int i = 0; i < 2; ++i) {
    m_connections[i]->propagate_observation(NULL, m_mean_field);
  }
  return m_connections[m_num_layers - 2]->find_label();
}

int Network::get_label() {
  return m_connections[m_num_layers - 2]->find_label();
}

gsl_vector *Network::extract_input_states() {
  return m_layers[0]->state(true);
}

void Network::dump_states(const char *filename) {
  std::ofstream f(filename);
  for(int i = 0; i < m_num_layers; ++i) {
    for(int j = 0; j < m_layer_sizes[i]; ++j) {
      f << "l\t" << i << "\t" << j << "\t" << m_layers[i]->get_bias(j) << std::endl;
    }
  }
  for(int i = 0; i < m_num_layers - 1; ++i) {
    for(int j = 0; j < m_layer_sizes[i+1]; ++j) {
      for(int k = 0; k < m_layer_sizes[i]; ++k) {
	if((i != 1) || (j < 500)) {
	  f << "c\t" << i << "\t" << j << "\t" << k << "\t" << m_connections[i]->get_weight(j, k) << std::endl;
	}
      }
    }
  }
  f.close();
}

void Network::load_states(const char *filename) {
  std::ifstream f(filename);
  for(int i = 0; i < m_num_layers; ++i) {
    for(int j = 0; j < m_layer_sizes[i]; ++j) {
      std::string t;
      int n_i;
      int n_j;
      double bias;
      f >> t >> n_i >> n_j >> bias;
      //      std::cout << t << " " << n_i << " " << n_j << " " << bias << std::endl;
      m_layers[i]->set_bias(j, bias);
    }
  }
  for(int i = 0; i < m_num_layers - 1; ++i) {
    for(int j = 0; j < m_layer_sizes[i+1]; ++j) {
      for(int k = 0; k < m_layer_sizes[i]; ++k) {
        if((i != 1) || (j < 500)) {
	  std::string t;
	  int n_i;
	  int n_j;
	  int n_k;
	  double weight;
	  f >> t >> n_i >> n_j >> n_k >> weight;
	  //  std::cout << t << " " << n_i << " " << n_j << " " << n_k << " " << weight << std::endl;
	  m_connections[i]->set_weight(j, k, weight);
	}
      }
    }
  }
  f.close();
}

void Network::greedily_train_layer(gsl_rng *rng, Dataset *training_data, int n, Schedule *schedule) {
  int input_size = m_layers[0]->size(false);
  gsl_vector *input_observations = gsl_vector_alloc(input_size); // TODO: Should be okay for dataset to own this rather than copying
  if(m_mean_field) {
    training_data->get_state(input_observations, schedule->active_image());
  } 
  else {
    training_data->get_sample(rng, input_observations, schedule->active_image());
  }
  m_layers[0]->set_state(input_observations);
  for(int i = 0; i < n; ++i) {
    m_connections[i]->propagate_observation(rng, m_mean_field);
  }
  if(n == 2) {
    m_layers[2]->set_label(training_data->get_label(schedule->active_image()));
  }
  m_connections[n]->perform_update_step(rng);
  gsl_vector_free(input_observations);
}

void Network::fine_tune(gsl_vector *observations, int label) {
  // \% UP-DOWN ALGORITHM
  // \%
  // \% the data and all biases are row vectors.
  // \% the generative model is: lab <--> top <--> pen --> hid --> vis
  // \% the number of units in layer foo is numfoo
  // \% weight matrices have names fromlayer tolayer
  // \% "rec" is for recognition biases and "gen" is for generative
  // \% biases.
  // \% for simplicity, the same learning rate, r, is used everywhere.

  // Todo - implement separation of forward and backwards probs
  // Todo - think about hard linear separation of incoming weights and labels as in the paper

  // TODO: Init deltas

  m_layers[0]->set_state(observations);
  // Propagate all the way to the top using sampling rater than mean field
  for(int i = 0; i < 3; ++i) {
    if(i == 2) {
      m_layers[2]->set_label(label);
    }

    m_connections[i]->propagate_observation(m_rng, false);
    m_connections[i]->find_probs_downwards();

    gsl_vector_add(m_layers[i]->delta_down, m_layers[i]->state(false));
    gsl_vector_sub(m_layers[i]->delta_down, m_layers[i]->p(false));
    gsl_vector_scale(m_layers[i]->delta_down, epsilon);

    gsl_blas_dger(m_layers[i]->delta_down, m_layers[i+1]->state(false), m_connections[i]->delta_down);
  }

  // \% POSITIVE PHASE STATISTICS FOR CONTRASTIVE DIVERGENCE
  // poslabtopstatistics = targets’ * waketopstates;
  // pospentopstatistics = wakepenstates’ * waketopstates;

  gsl_vector_add(m_layers[m_num_layers - 1]->delta, m_layers[m_num_layers - 1]->state(false));
  gsl_vector_add(m_layers[m_num_layers - 2]->delta, m_layers[m_num_layers - 2]->state(false));
    

  gsl_blas_dger(epsilon, m_layers[m_num_layers - 1]->state(false), m_layers[m_num_layers - 2]->state(true), m_connections[m_num_layers -2]->delta); // TODO: Confirm these are still coupled, plus learning rates

// Gibbs iterations 
  m_connections[m_num_layers - 2]->sample_layer(rng, 1000, label);

  // \% NEGATIVE PHASE STATISTICS FOR CONTRASTIVE DIVERGENCE
  // negpentopstatistics = negpenstates’*negtopstates;
  // neglabtopstatistics = neglabprobs’*negtopstates;
  
  gsl_vector_sub(m_layers[m_num_layers - 1]->delta, m_layers[m_num_layers - 1]->state(false));
  gsl_vector_sub(m_layers[m_num_layers - 2]->delta, m_layers[m_num_layers - 2]->state(false));
  gsl_vector_scale(m_layers[m_num_layers - 1]->delta, epsilon);
  gsl_vector_scale(m_layers[m_num_layers - 2]->delta, epsilon);

  gsl_blas_dger(-epsilon, m_layers[m_num_layers - 1]->state(false), m_layers[m_num_layers - 2]->state(true), m_connections[m_num_layers -2]->delta); // TODO: Confirm these are still coupled, plus learning rates

  // Propagate back down to the visible layer
  for(int i = m_num_lateyers - 3, i >= 0; --i) {
    m_connections[i]->propagate_hidden(m_rng, false);
    m_connections[i]->find_probs_upwards();

    gsl_vector_add(m_layers[i+1]->delta, m_layers[i+1]->state(false));
    gsl_vector_sub(m_layers[i+1]->delta, m_layers[i+1]->p(false));
    gsl_vector_scale(m_layers[i+1]->delta_down, epsilon);

    gsl_blas_dger(m_layers[i+1]->delta, m_layers[i]->state(false), m_connections[i]->delta);

  }

  // TODO: Commit deltas

  // \% PREDICTIONS
  // psleeppenstates = logistic(sleephidstates*hidpen + penrecbiases);
  // psleephidstates = logistic(sleepvisprobs*vishid + hidrecbiases);
  // pvisprobs = logistic(wakehidstates*hidvis + visgenbiases);
  // phidprobs = logistic(wakepenstates*penhid + hidgenbiases);

  // \% UPDATES TO GENERATIVE PARAMETERS
  // hidvis = hidvis + r*poshidstates’*(data-pvisprobs);
  // visgenbiases = visgenbiases + r*(data - pvisprobs);
  // penhid = penhid + r*wakepenstates’*(wakehidstates-phidprobs);
  // hidgenbiases = hidgenbiases + r*(wakehidstates - phidprobs);


  // \%UPDATES TO RECOGNITION/INFERENCE APPROXIMATION PARAMETERS
  // hidpen = hidpen + r*(sleephidstates’*(sleeppenstatespsleeppenstates));
  // penrecbiases = penrecbiases + r*(sleeppenstates-psleeppenstates);
  // vishid = vishid + r*(sleepvisprobs’*(sleephidstatespsleephidstates));
  // hidrecbiases = hidrecbiases + r*(sleephidstates-psleephidstates);


  // \% UPDATES TO TOP LEVEL ASSOCIATIVE MEMORY PARAMETERS
  // labtop = labtop + r*(poslabtopstatistics-neglabtopstatistics);
  // labgenbiases = labgenbiases + r*(targets - neglabprobs);
  // pentop = pentop + r*(pospentopstatistics - negpentopstatistics);
  // pengenbiases = pengenbiases + r*(wakepenstates - negpenstates);
  // topbiases = topbiases + r*(waketopstates - negtopstates);
}
