/*
Copyright 2023 Gregory Man.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

package controller

import (
	"context"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
	"strings"

	"k8s.io/apimachinery/pkg/runtime"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/log"

	lightingv1 "github.com/gregory-m/k8s-internals/controller/api/v1"
)

// LampReconciler reconciles a Lamp object
type LampReconciler struct {
	client.Client
	Scheme *runtime.Scheme
}

//+kubebuilder:rbac:groups=lighting.mops.ridewithvia.com,resources=lamps,verbs=get;list;watch;create;update;patch;delete
//+kubebuilder:rbac:groups=lighting.mops.ridewithvia.com,resources=lamps/status,verbs=get;update;patch
//+kubebuilder:rbac:groups=lighting.mops.ridewithvia.com,resources=lamps/finalizers,verbs=update

// Reconcile is part of the main kubernetes reconciliation loop which aims to
// move the current state of the cluster closer to the desired state.
func (r *LampReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	log := log.FromContext(ctx)
	log.Info("Starting Reconcile")

	var lamp lightingv1.Lamp
	if err := r.Get(ctx, req.NamespacedName, &lamp); err != nil {
		log.Error(err, "unable to fetch Lamp")
		return ctrl.Result{}, client.IgnoreNotFound(err)
	}

	currentColor, err := getCurrentColor(lamp)
	if err != nil {
		return ctrl.Result{}, err
	}

	log.Info(fmt.Sprintf("current lamp color %q", currentColor))

	if currentColor == lamp.Spec.Color {
		return ctrl.Result{}, nil
	}

	log.Info(fmt.Sprintf("updating lamp color from %q to %q ", currentColor, lamp.Spec.Color))

	err = updateColor(lamp)
	if err != nil {
		return ctrl.Result{}, err
	}

	return ctrl.Result{}, nil
}

func statusReq(host string) (*http.Request, error) {
	u := url.URL{
		Scheme: "http",
		Host:   host,
		Path:   "status",
	}

	req, err := http.NewRequest("GET", u.String(), nil)

	return req, err

}

func getCurrentColor(lamp lightingv1.Lamp) (color string, err error) {
	req, err := statusReq(lamp.Spec.Host)
	if err != nil {
		return "", fmt.Errorf("can't create lamp status request %w", err)
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return "", fmt.Errorf("can't get lamp status %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		err := fmt.Errorf("got %d code from status endpoint", resp.StatusCode)
		return "", fmt.Errorf("can't get lamp status %w", err)
	}

	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("can't get lamp status %w", err)
	}

	return strings.TrimSpace(string(body)), nil
}

func updateColor(lamp lightingv1.Lamp) error {
	params := url.Values{}
	params.Add("color", lamp.Spec.Color)
	body := strings.NewReader(params.Encode())
	updateURL := url.URL{
		Scheme: "http",
		Host:   lamp.Spec.Host,
		Path:   "update",
	}

	lampReq, err := http.NewRequest("POST", updateURL.String(), body)
	if err != nil {
		return fmt.Errorf("can't create lamp update request %w", err)

	}

	lampReq.Header.Set("Content-Type", "application/x-www-form-urlencoded")

	resp, err := http.DefaultClient.Do(lampReq)
	if err != nil {
		return fmt.Errorf("can't update lamp: %w", err)

	}

	if resp.StatusCode != http.StatusOK {
		err := fmt.Errorf("got %d code from update endpoint", resp.StatusCode)
		return fmt.Errorf("can't update lamp: %w", err)
	}
	defer resp.Body.Close()

	return nil
}

// SetupWithManager sets up the controller with the Manager.
func (r *LampReconciler) SetupWithManager(mgr ctrl.Manager) error {
	return ctrl.NewControllerManagedBy(mgr).
		For(&lightingv1.Lamp{}).
		Complete(r)
}
