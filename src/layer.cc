#include "layer.h"

#include <cmath>
#include <cstring>
#include <gsl/gsl_blas.h>
#include <iostream>

Layer::Layer(int size, bool labels) {
  m_size = size;
  m_labels = labels;
  m_biases = gsl_vector_calloc(m_size);
  m_biases_down = gsl_vector_calloc(m_size);
  m_deltas = gsl_vector_alloc(m_size);
  m_state = gsl_vector_alloc(m_size);
  m_p = gsl_vector_alloc(m_size);
  m_activation = gsl_vector_alloc(m_size);
  m_biases_view = gsl_vector_subvector(m_biases, 0, m_size - 10);
  m_deltas_view = gsl_vector_subvector(m_deltas, 0, m_size - 10);
  m_state_view = gsl_vector_subvector(m_state, 0, m_size - 10);
  m_p_view = gsl_vector_subvector(m_p, 0, m_size - 10);
  m_activation_view = gsl_vector_subvector(m_activation, 0, m_size - 10);
}

Layer::~Layer() {
  gsl_vector_free(m_biases);
  gsl_vector_free(m_biases_down);
  gsl_vector_free(m_deltas);
  gsl_vector_free(m_state);
  gsl_vector_free(m_p);
  gsl_vector_free(m_activation);
}

int Layer::size(bool ext) {
  if(ext || !m_labels) {
    return m_size;
  }
  else {
    return m_size - 10;
  }
}

double Layer::get_bias(int i) {
  return gsl_vector_get(m_biases, i);
}

void Layer::set_bias(int i, double v) {
  gsl_vector_set(m_biases, i, v);
}

gsl_vector *Layer::state(bool ext) {
  if(ext || !m_labels) {
    return m_state;
  }
  else {
    return &(m_state_view.vector);
  }
}

gsl_vector *Layer::activation(bool ext) {
  if(ext || !m_labels) {
    return m_activation;
  }
  else {
    return &(m_activation_view.vector);
  }
}

gsl_vector *Layer::p(bool ext) {
  if(ext || !m_labels) {
    return m_p;
  }
  else {
    return &(m_p_view.vector);
  }
}

gsl_vector *Layer::biases(bool ext) {
  if(ext || !m_labels) {
    return m_biases;
  }
  else {
    return &(m_biases_view.vector);
  }
}

gsl_vector *Layer::deltas(bool ext) {
  if(ext || !m_labels) {
    return m_deltas;
  }
  else {
    return &(m_deltas_view.vector);
  }
}

void Layer::reset_deltas() {
  // TODO: 'double buffering'
  gsl_vector_set_zero(m_deltas);
}

void Layer::commit_deltas() {
  gsl_vector_add(m_biases, m_deltas);
}

void Layer::transfer() {
  gsl_vector_memcpy(m_state, m_p);
}

void Layer::sample(gsl_rng *rng, bool ext) {
  if(m_labels && ext) {
    double probs[10];
    double norm = 0.0;
    for(int i = 0; i < 10; ++i) {
      probs[i] = exp(gsl_vector_get(m_activation, m_size - 10 + i));
      // std::cout << i << ": " << probs[i] << std::endl;
      norm += probs[i];
      gsl_vector_set(m_state, m_size - 10 + i, 0.0);
    }
    double target = gsl_rng_uniform(rng);
    int n = 0;
    double cumulative = probs[0] / norm;
    while((cumulative < target) && (n < 9)) {
      ++n;
      cumulative += probs[n] / norm;
    }
    // std::cout << "n = " << n << std::endl;
    gsl_vector_set(m_state, m_size - 10 + n, 1.0);
  }

  int max_idx;

  if(m_labels)
    max_idx = m_size - 10;
  else
    max_idx = m_size;

  for(int i = 0; i < max_idx; ++i) {
    gsl_vector_set(m_state, i, gsl_rng_uniform(rng) < gsl_vector_get(m_p, i));
  }
}

void Layer::set_state(const gsl_vector *state) {
  gsl_vector_memcpy(m_state, state);
}

void Layer::activate_from_bias() {
  for(int i = 0; i < m_size; ++i) {
    gsl_vector_set(m_p, i, 1.0 / (1.0 + exp(-gsl_vector_get(m_biases, i))));
    //    std::cout << gsl_vector_get(m_p, i) << std::endl;
  }
}
  
void Layer::set_label(int label) {
  for(int i = 0; i < 10; ++i) {
    if(label == i) {
      gsl_vector_set(m_state, m_size - 10 + i, 1.0);
    }
    else {
      gsl_vector_set(m_state, m_size - 10 + i, 0.0);
    }
  }
}  

int Layer::most_probable_label() {
  int idx = 0;
  double v = gsl_vector_get(m_p, idx);
  for(int i = 501; i < 510; ++i) {
    if(gsl_vector_get(m_p, i) > v) {
      v = gsl_vector_get(m_p, i);
      idx = i - 500;
    }
  }
  return idx;
}
